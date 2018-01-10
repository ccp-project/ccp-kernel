#include <linux/module.h>
#include <net/tcp.h>
#include "tcp_ccp.h"

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
        pr_info("ccp: Setting new rate %d Mbit/s (%d Bps) (cwnd %llu)\n", ca->rate / 125000, ca->rate, segs_in_flight + 3);
        /* Add few more segments to segs_to_flight to prevent rate underflow due to 
         * temporary RTT fluctuations. */
        tp->snd_cwnd = segs_in_flight + 3;
    }
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

static u64 ewma(u64 old, u64 new) {
    if (old == 0) {
        return new;
    }

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
        .loss = rs->losses,
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

	u32 curr_time = tcp_jiffies32;

	if (curr_time>ca->prev_mmt_time) {
		ca->mmt.rin = ewma(ca->mmt.rin, ca->prev_rin);
		ca->mmt.rout = ewma(ca->mmt.rout, ca->prev_rout);
	}
	ca->prev_mmt_time = curr_time;
	
	//pr_info("new measurement: ack %u, rtt %u, rin %llu, rout %llu, time %u\n", 
    //        curr_mmt.ack,
    //       curr_mmt.rtt,
    //       curr_mmt.rin,
    //       curr_mmt.rout,
	//		curr_time
	//);
	

    ca->mmt.ack = curr_mmt.ack; // max()
    ca->mmt.rtt = ewma(ca->mmt.rtt, curr_mmt.rtt);
	ca->mmt.loss = curr_mmt.loss;
   	ca->prev_rin = curr_mmt.rin;
    ca->prev_rout = curr_mmt.rout;
    
    //pr_info("curr measurement: ack %u, rtt %u, rin %llu, rout %llu\n", 
    //        ca->mmt.ack,
    //        ca->mmt.rtt,
    //        ca->mmt.rin,
    //        ca->mmt.rout
    //);
     
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
    struct tcp_sock *tp;
    struct ccp *cpl;
    struct ccp_measurement init_mmt = {
        .ack = 0,
        .rtt = 0,
        .rin = 0, /* send bandwidth in bytes per second */
        .rout = 0, /* recv bandwidth in bytes per second */
        .loss = 0,
    };

    // store initialized netlink sock ptr in connection state
    tp = tcp_sk(sk);
    cpl = inet_csk_ca(sk);
    cpl->ccp_index = ccp_connection_start(sk);
    pr_info("ccp: starting connection %d", cpl->ccp_index);
    
    cpl->next_event_time = tcp_jiffies32;
    cpl->currPatternEvent = 0;
    cpl->numPatternEvents = 0;
    cpl->last_drop_state = NO_DROP;
    cpl->num_loss = 0;
   	cpl->prev_mmt_time = 0; 
	cpl->prev_rin = 0;
	cpl->prev_rout = 0;
	memcpy(&(cpl->mmt), &init_mmt, sizeof(struct ccp_measurement));
	cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);

    // send to CCP:
    // index of pointer back to this sock for IPC callback
    // first ack to expect
    ok = nl_send_conn_create(cpl->ccp_index, tp->snd_una);
    if (ok < 0) {
        pr_info("failed to send create message: %d", ok);
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    pr_info("ccp: freeing connection %d", cpl->ccp_index);
    ccp_connection_free(cpl->ccp_index);
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
    struct sock *nl_sk;
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv,
    };

    printk(KERN_INFO "Init ccp\n");
    ok = ccp_init_connection_map();
    if (ok < 0) {
        return -1;
    }

    nl_sk = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating netlink socket.\n");
        return -1;
    }
   
    set_ccp_nl_sk(nl_sk);
    printk(KERN_INFO "init NL\n");
    
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
