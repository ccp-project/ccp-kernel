#include "tcp_ccp.h"
#include "ccp_nl.h"
#include "libccp/ccp.h"

#include <linux/module.h>
#include <net/tcp.h>

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

void ccp_set_pacing_rate(struct sock *sk) {
    struct ccp *ca = inet_csk_ca(sk);
    sk->sk_pacing_rate = ca->rate;
    pr_info("ccp: Setting new rate %d Mbit/s (%d Bps)\n", ca->rate / 125000, ca->rate);
}

static int rate_sample_valid(const struct rate_sample *rs) {
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

static inline void get_sock_from_ccp(
    struct sock **sk,
    struct ccp_connection *dp
) {
    *sk = (struct sock*) ccp_get_impl(dp);
}

static void do_set_cwnd(
    struct ccp_connection *dp, 
    uint32_t cwnd
) {
    struct sock *sk;
    struct tcp_sock *tp;
    get_sock_from_ccp(&sk, dp);
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
    get_sock_from_ccp(&sk, dp);
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
    uint64_t newrate;
    get_sock_from_ccp(&sk, dp);
    ca = inet_csk_ca(sk);

    // factor is * 100
    newrate = ca->rate * factor;
    do_div(newrate, 100);
    printk(KERN_INFO "rate -> %llu\n", newrate);
    ca->rate = (u32) newrate;
    ccp_set_pacing_rate(sk);
}

static struct ccp_primitives *get_ccp_primitives(
    struct ccp_connection *dp
) {
    struct sock *sk;
    struct ccp *ca;
    get_sock_from_ccp(&sk, dp);
    ca = inet_csk_ca(sk);

    return &(ca->mmt);
}

static u32 ccp_now(void) {
    return tcp_time_stamp;
}

static u32 ccp_after(u32 us) {
    return tcp_time_stamp + usecs_to_jiffies(us);
}

// in dctcp code, in ack event used for ecn information per packet
// TODO: possibly use this function for updating more primitives
void tcp_ccp_in_ack_event(struct sock *sk, u32 flags) {
    struct ccp *ca = inet_csk_ca(sk);
    if (flags & CA_ACK_ECE) {
        ca->mmt.ecn = true;
    } else {
        ca->mmt.ecn = false;
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_in_ack_event);

void load_primitives( struct sock *sk, const struct rate_sample *rs) {
    // load the primitive registers of the rate sample - convert all to u64
    // raw values, not averaged
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    u64 ack = (u64)(tp->snd_una);
    u64 rtt = (u64)(rs->rtt_us);
    u64 loss = (u64)(rs->losses);
    u64 rin = 0; // send bandwidth in bytes per second
    u64 rout = 0; // recv bandwidth in bytes per second
    u64 cwnd = tp->snd_cwnd;
    int measured_valid_rate = rate_sample_valid(rs);
    if ( measured_valid_rate == 0 ) {
       rin = rout  = (u64)rs->delivered * MTU * S_TO_US;
       do_div(rin, rs->snd_int_us);
       do_div(rout, rs->rcv_int_us);
    } else {
        return;
    }
    ca->mmt.ack = ack;
    ca->mmt.loss = loss;
    ca->mmt.rcvrate = rout;
    ca->mmt.rtt = rtt;
    ca->mmt.sndcwnd = cwnd;
    ca->mmt.sndrate = rin;
    return;
}

void tcp_ccp_cong_control(struct sock *sk, const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_connection *dp = ca->dp;

    // load primitive registers
    load_primitives(sk, rs);
    
    if (dp != NULL) {
        ccp_invoke(dp);
    } else {
        pr_info("ccp: ccp_connection not initialized");
    }
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
    struct ccp *cpl = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    switch (new_state) {
        case TCP_CA_Recovery:
            printk(KERN_INFO "entered TCP_CA_Recovery (dupack drop)\n");
            break;
        case TCP_CA_Loss:
            printk(KERN_INFO "entered TCP_CA_Loss (timeout drop)\n");
            cpl->mmt.loss += tp->snd_cwnd; // the whole cwnd was lost
            break;
        case TCP_CA_CWR:
            printk(KERN_INFO "entered TCP_CA_CWR (ecn drop)\n");
            break;
        default:
            printk(KERN_INFO "entered TCP normal state\n");
            return;
    }
    
    //send_drop_notif(cpl->dp, dtype);
}
EXPORT_SYMBOL_GPL(tcp_ccp_set_state);

void tcp_ccp_init(struct sock *sk) {
    struct ccp *cpl;
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp_primitives init_mmt = {
        .ack = tp->snd_una,
        .ecn = false,
        .loss = 0,
        .rcvrate = 0,
        .rtt = 0,
        .sndcwnd = tp->snd_cwnd,
        .sndrate = 0,
    };
    struct ccp_connection dp = {
        .set_cwnd = &do_set_cwnd,
        .set_rate_abs = &do_set_rate_abs,
        .set_rate_rel = &do_set_rate_rel,
        .get_ccp_primitives = &get_ccp_primitives,
        .send_msg = &nl_sendmsg,
        .now = &ccp_now,
        .after_usecs = &ccp_after
    };
    
    cpl = inet_csk_ca(sk);
    cpl->mmt = init_mmt;

    // copy sk pointer into impl field of dp
    ccp_set_impl(&dp, (void*) sk);

    cpl->dp = ccp_connection_start(&dp);
    if (cpl->dp == NULL) {
        pr_info("ccp: start connection failed\n");
    } else {
        pr_info("ccp: starting connection %d", cpl->dp->index);
    }

    // if no ecn support
    if (!(tp->ecn_flags & TCP_ECN_OK)) {
        INET_ECN_dontxmit(sk);
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    if (cpl->dp != NULL) {
        pr_info("ccp: freeing connection %d", cpl->dp->index);
        ccp_connection_free(cpl->dp->index);
    } else {
        pr_info("ccp: already freed");
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_release);

struct tcp_congestion_ops tcp_ccp_congestion_ops = {
    .flags = TCP_CONG_NEEDS_ECN,
    .in_ack_event = tcp_ccp_in_ack_event,
    .name = "ccp",
    .owner = THIS_MODULE,
    .init = tcp_ccp_init,
    .release = tcp_ccp_release,
    .ssthresh = tcp_ccp_ssthresh,
    //.cong_avoid = tcp_ccp_cong_avoid,
    .cong_control = tcp_ccp_cong_control,
    .undo_cwnd = tcp_ccp_undo_cwnd,
    .set_state = tcp_ccp_set_state,
    .pkts_acked = tcp_ccp_pkts_acked
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
