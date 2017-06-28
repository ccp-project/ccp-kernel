#include <linux/module.h>
#include <net/tcp.h>

#include "tcp_ccp.h"
#include "ccp_nl.h"
#include "stateMachine.h"

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

void ccp_set_pacing_rate(struct sock *sk) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    u64 segs_in_flight; /* desired cwnd as rate * rtt */
    sk->sk_pacing_rate = ca->rate;
    if (likely(ca->mmt.rtt > 0)) {
        segs_in_flight = (u64)ca->rate * ca->mmt.rtt;
        do_div(segs_in_flight, MTU);
        do_div(segs_in_flight, S_TO_US);
        pr_info("ccp: Setting new rate %d Mbit/s (%d bps) (cwnd %llu)\n", ca->rate / 125000, ca->rate, segs_in_flight + 3);
        /* Add few more segments to segs_to_flight to prevent rate underflow due to 
         * temporary RTT fluctuations. */
        tp->snd_cwnd = segs_in_flight + 3;
    }
}

static int rate_sample_valid(const struct rate_sample *rs)
{
  int ret = 0;
  if ((rs->delivered > 0) && (rs->snd_int_us > 0) && (rs->rcv_int_us > 0) && (rs->interval_us > 0))
    return 0;
  if (rs->delivered <= 0)
    ret |= 1;
  if (rs->snd_int_us <= 0)
    ret |= 2;
  if (rs->rcv_int_us <= 0)
    ret |= 4;
  if (rs->interval_us <= 0)
    ret |= 8;
  return ret;
}

static u64 ewma(u64 old, u64 new) {
    return ((new * CCP_EWMA_RECENCY) +
        (old * (CCP_FRAC_DENOM-CCP_EWMA_RECENCY))) / CCP_FRAC_DENOM;
}

void tcp_ccp_cong_control(struct sock *sk, const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    // TODO custom fold functions (for now, default only all fields)
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_measurement curr_mmt = {
        .ack = tp->snd_una,
        .rtt = rs->rtt_us,
        .rin = 0, /* send bandwidth in bytes per second */
        .rout = 0, /* recv bandwidth in bytes per second */
    };

    int measured_valid_rate = rate_sample_valid(rs);
    if (measured_valid_rate == 0) {
        curr_mmt.rin = curr_mmt.rout = (u64)rs->delivered * MTU * S_TO_US;
        do_div(curr_mmt.rin, rs->snd_int_us);
        do_div(curr_mmt.rout, rs->rcv_int_us);
    } else {
        return;
    }
    
    //pr_info("ccp: rate_calculation: delivered %llu, MTU %d, S_TO_US %d, rcv_bw_bps %llu, rcv_int_us %lu, snd_bw_bps %llu, snd_int_us %lu rs_rtt %ld\n",
    //        (u64) rs->delivered, MTU, S_TO_US, ca->mmt.rout, rs->rcv_int_us, ca->mmt.rin, rs->snd_int_us, rs->rtt_us);

    ca->mmt.ack = curr_mmt.ack; // max()
    ca->mmt.rtt = ewma(ca->mmt.rtt, curr_mmt.rtt);
    ca->mmt.rin = ewma(ca->mmt.rin, curr_mmt.rin);
    ca->mmt.rout = ewma(ca->mmt.rout, curr_mmt.rout);
     
    // rate control state machine
    sendStateMachine(sk);
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
    struct ccp *cpl;
    enum drop_type dtype;
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
            printk(KERN_INFO "TCP normal state\n");
            return;
    }

    cpl = inet_csk_ca(sk);
    if (!cpl->created) {
        return;
    }

    nl_send_drop_notif(cpl->nl_sk, cpl->ccp_index, dtype);
}
EXPORT_SYMBOL_GPL(tcp_ccp_set_state);

void tcp_ccp_init(struct sock *sk) {
    struct tcp_sock *tp;
    struct sock *nl_sk;
    struct ccp *cpl;
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv,
    };

    printk(KERN_INFO "init NL\n");

    nl_sk = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating socket.\n");
        return;
    }

    // store initialized netlink sock ptr in connection state
    tp = tcp_sk(sk);
    cpl = inet_csk_ca(sk);
    // if returned 0, don't communicate with ccp
    cpl->ccp_index = ccp_connection_start(sk);
    cpl->nl_sk = nl_sk;
    cpl->next_event_time = tcp_time_stamp;
    cpl->currPatternEvent = 0;
    cpl->numPatternEvents = 0;

    // send to CCP:
    // index of pointer back to this sock for IPC callback
    // first ack to expect
    check_nlsk_created(cpl, tp->snd_una);
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    printk(KERN_INFO "exit NL\n");
    ccp_connection_free(cpl->ccp_index);
    netlink_kernel_release(cpl->nl_sk);
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

    printk(KERN_INFO "Init ccp\n");

    ok = ccp_init_connection_map();
    if (ok < 0) {
        return -1;
    }
    
    return tcp_register_congestion_control(&tcp_ccp_congestion_ops);
}

static void __exit tcp_ccp_unregister(void) {
    printk(KERN_INFO "Exit ccp\n");
    ccp_free_connection_map();
    tcp_unregister_congestion_control(&tcp_ccp_congestion_ops);
}

module_init(tcp_ccp_register);
module_exit(tcp_ccp_unregister);

MODULE_AUTHOR("Akshay Narayan <akshayn@mit.edu>");
MODULE_DESCRIPTION("Kernel datapath for a congestion control plane");
MODULE_LICENSE("GPL");
