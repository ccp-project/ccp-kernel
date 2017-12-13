#include "ccp_nl.h"
#include "libccp/ccp.h"
#include "libccp/serialize.h"

#include <linux/module.h>
#include <net/tcp.h>

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

void ccp_set_pacing_rate(struct sock *sk) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    u64 segs_in_flight; /* desired cwnd as rate * rtt */
    sk->sk_pacing_rate = ca->rate;
    pr_info("ccp: Setting new rate %d Mbit/s (%d Bps)\n", ca->rate / 125000, ca->rate);

    //if (likely(ca->mmt.rtt > 0)) {
    //    segs_in_flight = (u64)ca->rate * ca->mmt.rtt;
    //    do_div(segs_in_flight, MTU);
    //    do_div(segs_in_flight, S_TO_US);
    //    // Add few more segments to segs_to_flight to prevent rate underflow due to 
    //    // temporary RTT fluctuations.
    //    tp->snd_cwnd = segs_in_flight + 3;
    //}
}

static int rate_sample_valid(const struct rate_sample *rs)
{
  int ret = 0;
  if (rs->delivered <= 0)
    ret |= 1;
  if (rs->snd_int_us <= 0)
    ret |= 1 << 1;
  if (rs->rcv_int_us <= 0)
    ret |= 1 << 2;
  if (rs->interval_us <= 0)
    ret |= 1 << 3;
  if (rs->rtt_us <= 0)
    ret |= 1 << 4;
  return ret;
}

static void get_sock_from_ccp(
    struct sock *sk,
    struct ccp_connection *dp
) {
    void *impl;
    impl = ccp_get_impl(dp);
    memcpy(sk, impl, sizeof(struct sock*));
}

static void do_set_cwnd(
    struct ccp_connection *dp, 
    uint32_t cwnd
) {
    struct sock *sk;
    struct tcp_sock *tp;
    get_sock_from_ccp(sk, dp);
    tp = tcp_sk(sk);

    // translate cwnd value back into packets
    cwnd /= tp->mss_cache;
    printk(KERN_INFO "cwnd %d -> %d (mss %d)\n", tp->snd_cwnd, cwnd, tp->mss_cache);
    tp->snd_cwnd = cwnd;
}

static void do_set_rate_abs(
    struct ccp_connection *dp, 
    uint32_t rate
) {
    struct sock *sk;
    struct ccp *ca;
    get_sock_from_ccp(sk, dp);
    ca = inet_csk_ca(sk);

    printk(KERN_INFO "rate (Bytes/s) -> %u\n", rate);
    ca->rate = rate;
    ccp_set_pacing_rate(sk);
}

static void do_set_rate_rel(
    struct ccp_connection *dp, 
    uint32_t factor
) {
    struct sock *sk;
    struct ccp *ca;
    get_sock_from_ccp(sk, dp);
    ca = inet_csk_ca(sk);

    // factor is * 100
    uint64_t newrate = ca->rate * factor;
    do_div(newrate, 100);
    printk(KERN_INFO "rate -> %llu\n", newrate);
    ca->rate = (u32) newrate;
    ccp_set_pacing_rate(sk);
}

static struct ccp_measurement *get_ccp_primitives(
    struct ccp_connection *dp
) {
    struct sock *sk;
    struct ccp *ca;
    get_sock_from_ccp(sk, dp);
    ca = inet_csk_ca(sk);

    return ca->mmt;
}

void tcp_ccp_cong_control(struct sock *sk, const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    // TODO custom fold functions (for now, default only all fields)

    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_connection *dp = ca->dp;
    struct ccp_instruction_list* instr = ccp_instruction_list_lookup(ca->ccp_index);

    // load primitive registers
    load_primitives(sk, rs);
    // update the signal state
    update_state_registers(ca);


    pr_info("Prim ack: %llu, our ack: %llu\n", ca->mmt.ack, instr->state_registers[ACK]);
    pr_info("Prim rtt: %llu, our rtt: %llu\n", ca->mmt.rtt, instr->state_registers[RTT]);
    pr_info("Prim rin: %llu, our rin: %llu\n", ca->mmt.rin, instr->state_registers[RIN]);
    pr_info("Prim rout: %llu, our rout: %llu\n", ca->mmt.rout, instr->state_registers[ROUT]);
    pr_info("Prim loss: %llu, our loss : %llu\n", ca->mmt.loss, instr->state_registers[LOSS]); 
    
    ccp_invoke(dp);
}
EXPORT_SYMBOL_GPL(tcp_ccp_cong_control);

/* Slow start threshold is half the congestion window (min 2) */
u32 tcp_ccp_ssthresh(struct sock *sk) {
    const struct tcp_sock *tp = tcp_sk(sk);

    return max(tp->snd_cwnd >> 1U, 2U);
}
EXPORT_SYMBOL_GPL(tcp_ccp_ssthresh);

u32 tcp_ccp_undo_cwnd(struct sock *sk) {
    const struct tcp_sock *tp = tcp_sk(sk);

    return max(tp->snd_cwnd, tp->snd_ssthresh << 1);
}
EXPORT_SYMBOL_GPL(tcp_ccp_undo_cwnd);

void tcp_ccp_pkts_acked(struct sock *sk, const struct ack_sample *sample) {
    struct ccp *cpl;
    s32 sampleRTT;

    cpl = inet_csk_ca(sk);
    sampleRTT = sample->rtt_us;
    //printk(KERN_INFO "pkt sample rtt %d us\n", sampleRTT);
}
EXPORT_SYMBOL_GPL(tcp_ccp_pkts_acked);

/*
 * Detect drops.
 *
 * TCP_CA_Loss -> a timeout happened
 * TCP_CA_Recovery -> an isolated loss (3x dupack) happened.
 * TCP_CA_CWR -> got an ECN
 */
void tcp_ccp_set_state(struct sock *sk, u8 new_state) {
    enum drop_type dtype;
    struct ccp *cpl = inet_csk_ca(sk);
    switch (new_state) {
        case TCP_CA_Recovery:
            printk(KERN_INFO "entered TCP_CA_Recovery (dupack drop)\n");
            dtype = DROP_DUPACK;
            break;
        case TCP_CA_Loss:
            printk(KERN_INFO "entered TCP_CA_Loss (timeout drop)\n");
            dtype = DROP_TIMEOUT;
            break;
        case TCP_CA_CWR:
            printk(KERN_INFO "entered TCP_CA_CWR (ecn drop)\n");
            dtype = DROP_ECN;
            break;
        default:
            printk(KERN_INFO "entered TCP normal state\n");
            cpl->last_drop_state = NO_DROP;
            return;
    }

    if (cpl->last_drop_state == dtype) {
        return;
    }

    cpl->last_drop_state = dtype;
    nl_send_drop_notif(cpl->ccp_index, dtype);
}
EXPORT_SYMBOL_GPL(tcp_ccp_set_state);

void tcp_ccp_init(struct sock *sk) {
    int ok;
    void *impl;
    struct tcp_sock *tp;
    struct ccp_connection *dp;
    struct ccp *cpl;
    //struct ccp_instruction_list *instr_list;
    struct ccp_primitives init_mmt;

    dp = {
        .index = 0,

        .set_cwnd = &do_set_cwnd,
        .set_rate_abs = &do_set_rate_abs,
        .set_rate_rel = &do_set_rate_rel,

        .get_ccp_primitives = &get_ccp_primitives,

        .send_msg = &nl_sendmsg,
    };
    
    tp = tcp_sk(sk);
    init_mmt = {
        .ack = tp->snd_una,
        .rtt = 0,
        .loss = 0,
        .rin = 0,
        .rout = 0
    };
    
    cpl->last_drop_state = NO_DROP;
    memcpy(&(cpl->mmt), &init_mmt, sizeof(struct ccp_primitives));

    // copy sk pointer into impl field of dp
    ccp_set_impl(dp, (void*) sk, sizeof(sk));

    cpl = inet_csk_ca(sk);
    cpl->dp = ccp_connection_start(dp);
    pr_info("ccp: starting connection %d", cpl->dp->index);
    pr_info("Size of ccp struct is %lu\n", sizeof(struct ccp));
    //instr_list = ccp_instruction_list_lookup(cpl->ccp_index);
    //pr_info("Num instructions in thing is %d\n", instr_list->num_instructions);
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    pr_info("ccp: freeing connection %d", cpl->dp->index);
    ccp_connection_free(cpl->dp);
}
EXPORT_SYMBOL_GPL(tcp_ccp_release);

struct tcp_congestion_ops tcp_ccp_congestion_ops = {
    .flags = TCP_CONG_NON_RESTRICTED,
    .name = "ccp",
    .owner = THIS_MODULE,
    .init = tcp_ccp_init,
    .release = tcp_ccp_release,
    .ssthresh = tcp_ccp_ssthresh,
    //.cong_avoid = tcp_ccp_cong_avoid,
    .cong_control = tcp_ccp_cong_control,
    .undo_cwnd = tcp_ccp_undo_cwnd,
    .set_state = tcp_ccp_set_state,
    .pkts_acked = tcp_ccp_pkts_acked,
};

static int __init tcp_ccp_register(void) {
    int ok;

    ok = ccp_nl_sk(&ccp_read_msg); // TODO
    if (ok < 0) {
        return -1;
    }

    printk(KERN_INFO "init NL\n");

    ok = ccp_init_connection_map();
    if (ok < 0) {
        return -1;
    }

    printk(KERN_INFO "Init ccp\n");
    //ok = ccp_init_fold_map();
    //if (ok < 0) {
    //    return -1;
    //}
    
    return tcp_register_congestion_control(&tcp_ccp_congestion_ops);
}

static void __exit tcp_ccp_unregister(void) {
    printk(KERN_INFO "Exit ccp\n");
    ccp_free_connection_map();
    free_ccp_nl_sk();
    tcp_unregister_congestion_control(&tcp_ccp_congestion_ops);
}

module_init(tcp_ccp_register);
module_exit(tcp_ccp_unregister);

MODULE_AUTHOR("Akshay Narayan <akshayn@mit.edu>");
MODULE_DESCRIPTION("Kernel datapath for a congestion control plane");
MODULE_LICENSE("GPL");
