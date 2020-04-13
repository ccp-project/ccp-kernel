#include "tcp_ccp.h"
#include "libccp/ccp.h"

#if __KERNEL_VERSION_MINOR__ <= 14 && __KERNEL_VERSION_MINOR__ >= 13
#define COMPAT_MODE
#elif __KERNEL_VERSION_MAJOR__ > 4
#define RATESAMPLE_MODE
#elif __KERNEL_VERSION_MAJOR__ == 4 && __KERNEL_VERSION_MINOR__ >= 19
#define RATESAMPLE_MODE
#endif

#define IPC_NETLINK 0
#define IPC_CHARDEV 1

#if __IPC__ == IPC_NETLINK
#include "ccp_nl.h"
#elif __IPC__ == IPC_CHARDEV
#include "ccpkp/ccpkp.h"
#endif

#include <linux/module.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <net/tcp.h>

#define CCP_FRAC_DENOM 10
#define CCP_EWMA_RECENCY 6

// Global internal state -- allocated during ccp_init and freed in ccp_free.
struct ccp_datapath *kernel_datapath;

void ccp_set_pacing_rate(struct sock *sk, uint32_t rate) {
    sk->sk_pacing_rate = rate;
}

static int rate_sample_valid(const struct rate_sample *rs) {
  int ret = 0;
  if (rs->delivered <= 0)
    ret |= 1;
  if (rs->interval_us <= 0)
    ret |= 1 << 1;
  if (rs->rtt_us <= 0)
    ret |= 1 << 2;
  return ret;
}

static inline void get_sock_from_ccp(
    struct sock **sk,
    struct ccp_connection *conn
) {
    *sk = (struct sock*) ccp_get_impl(conn);
}

static void do_set_cwnd(
    struct ccp_connection *conn, 
    uint32_t cwnd
) {
    struct sock *sk;
    struct tcp_sock *tp;
    get_sock_from_ccp(&sk, conn);
    tp = tcp_sk(sk);

    // translate cwnd value back into packets
    cwnd /= tp->mss_cache;
    tp->snd_cwnd = cwnd;
}

static void do_set_rate_abs(
    struct ccp_connection *conn, 
    uint32_t rate
) {
    struct sock *sk;
    get_sock_from_ccp(&sk, conn);
    ccp_set_pacing_rate(sk, rate);
}

struct timespec64 tzero;
static u64 ccp_now(void) {
    struct timespec64 now, diff;
    ktime_get_real_ts64(&now);
    diff = timespec64_sub(now, tzero);
    return timespec64_to_ns(&diff);
}

static u64 ccp_since(u64 then) {
    struct timespec64 now, then_ts, diff;
    ktime_get_real_ts64(&now);
    then_ts = tzero;
    timespec64_add_ns(&then_ts, then);
    diff = timespec64_sub(now, then_ts);
    return timespec64_to_ns(&diff) / NSEC_PER_USEC;
}

static u64 ccp_after(u64 us) {
    struct timespec64 now;
    ktime_get_real_ts64(&now);
    now = timespec64_sub(now, tzero);
    timespec64_add_ns(&now, us * NSEC_PER_USEC);
    return timespec64_to_ns(&now);
}

// in dctcp code, in ack event used for ecn information per packet
void tcp_ccp_in_ack_event(struct sock *sk, u32 flags) {
    // according to tcp_input, in_ack_event is called before cong_control, so mmt.ack has old ack value
    const struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_primitives *mmt;
    u32 acked_bytes;
#ifdef COMPAT_MODE
    int i=0;
    struct sk_buff *skb = tcp_write_queue_head(sk);
    struct tcp_skb_cb *scb;
#endif

    if (ca->conn == NULL) {
        pr_info("[ccp] ccp_connection not initialized");
        return;
    }

#ifdef COMPAT_MODE
    for (i=0; i < MAX_SKB_STORED; i++) {
        ca->skb_array[i].first_tx_mstamp = 0;
        ca->skb_array[i].interval_us = 0;
    }

    for (i=0; i < MAX_SKB_STORED; i++) {
        if (skb) {
            scb = TCP_SKB_CB(skb);
            ca->skb_array[i].first_tx_mstamp = skb->skb_mstamp;
            ca->skb_array[i].interval_us = tcp_stamp_us_delta(skb->skb_mstamp, scb->tx.first_tx_mstamp);
            skb = skb->next;
        }
    }
#endif
    
    mmt = &ca->conn->prims;
    acked_bytes = tp->snd_una - ca->last_snd_una;
    ca->last_snd_una = tp->snd_una;
    if (acked_bytes) {
        if (flags & CA_ACK_ECE) {
            mmt->ecn_bytes = (u64)acked_bytes;
            mmt->ecn_packets = (u64)acked_bytes / tp->mss_cache;
        } else {
            mmt->ecn_bytes = 0;
            mmt->ecn_packets = 0;
        }
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_in_ack_event);

/* load the primitive registers of the rate sample - convert all to u64
 * raw values, not averaged
 */
int load_primitives(struct sock *sk, const struct rate_sample *rs) {
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_primitives *mmt = &ca->conn->prims;
#ifdef COMPAT_MODE
    int i=0;
#endif

    u64 rin = 0; // send bandwidth in bytes per second
    u64 rout = 0; // recv bandwidth in bytes per second
    u64 ack_us = 0;
    u64 snd_us = 0;
    int measured_valid_rate = rate_sample_valid(rs);
    if ( measured_valid_rate != 0 ) {
        return -1;
    }

#ifdef COMPAT_MODE
    // receive rate
    ack_us = tcp_stamp_us_delta(tp->tcp_mstamp, rs->prior_mstamp);

    // send rate
    for (i=0; i < MAX_SKB_STORED; i++) {
        if (ca->skb_array[i].first_tx_mstamp == tp->first_tx_mstamp) {
            snd_us = ca->skb_array[i].interval_us;
            break;
        }
    }
#endif
#ifdef RATESAMPLE_MODE
    ack_us = rs->rcv_interval_us;
    snd_us = rs->snd_interval_us;
#endif

    if (ack_us != 0 && snd_us != 0) {
        rin = rout = (u64)rs->delivered * MTU * S_TO_US;
        do_div(rin, snd_us);
        do_div(rout, ack_us);
    }

    mmt->bytes_acked = tp->bytes_acked - ca->last_bytes_acked;
    ca->last_bytes_acked = tp->bytes_acked;

    mmt->packets_misordered = tp->sacked_out - ca->last_sacked_out;
    if (tp->sacked_out < ca->last_sacked_out) {
        mmt->packets_misordered = 0;
    } else {
        mmt->packets_misordered = tp->sacked_out - ca->last_sacked_out;
    }

    ca->last_sacked_out = tp->sacked_out;

    mmt->packets_acked = rs->acked_sacked - mmt->packets_misordered;
    mmt->bytes_misordered = mmt->packets_misordered * tp->mss_cache;
    mmt->lost_pkts_sample = rs->losses;
    mmt->rtt_sample_us = rs->rtt_us;
    if ( rin != 0 ) {
        mmt->rate_outgoing = rin;
    }

    if ( rout != 0 ) {
        mmt->rate_incoming = rout;
    }

    mmt->bytes_in_flight = tcp_packets_in_flight(tp) * tp->mss_cache;
    mmt->packets_in_flight = tcp_packets_in_flight(tp);
    if (tp->snd_cwnd <= 0) {
        return -1;
    }

    mmt->snd_cwnd = tp->snd_cwnd * tp->mss_cache;

    if (unlikely(tp->snd_una > tp->write_seq)) {
        mmt->bytes_pending = ((u32) ~0U) - (tp->snd_una - tp->write_seq);
    } else {
        mmt->bytes_pending = (tp->write_seq - tp->snd_una);
    }

    return 0;
}

void tcp_ccp_cong_control(struct sock *sk, const struct rate_sample *rs) {
    // aggregate measurement
    // state = fold(state, rs)
    int ok;
    struct ccp *ca = inet_csk_ca(sk);
    struct ccp_connection *conn = ca->conn;

#if __IPC__ == IPC_CHARDEV
        ccpkp_try_read();
#endif

    if (conn != NULL) {
        // load primitive registers
        ok = load_primitives(sk, rs);
        if (ok < 0) {
            return;
        }

        ccp_invoke(conn);
        ca->conn->prims.was_timeout = false;
    } else {
        pr_info("[ccp] ccp_connection not initialized");
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
    switch (new_state) {
        case TCP_CA_Loss:
            if (cpl->conn != NULL) {
                cpl->conn->prims.was_timeout = true;
            }
            ccp_invoke(cpl->conn);
            return;
        case TCP_CA_Recovery:
        case TCP_CA_CWR:
        default:
            break;
    }
            
    if (cpl->conn != NULL) {
        cpl->conn->prims.was_timeout = false;
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_set_state);

void tcp_ccp_init(struct sock *sk) {
    struct ccp *cpl;
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp_datapath_info dp_info = {
        .init_cwnd = tp->snd_cwnd * tp->mss_cache,
        .mss = tp->mss_cache,
        .src_ip = tp->inet_conn.icsk_inet.inet_saddr,
        .src_port = tp->inet_conn.icsk_inet.inet_sport,
        .dst_ip = tp->inet_conn.icsk_inet.inet_daddr,
        .dst_port = tp->inet_conn.icsk_inet.inet_dport,
        .congAlg = "reno",
    };

    pr_info("[ccp] new flow\n");
    
    cpl = inet_csk_ca(sk);
    cpl->last_snd_una = tp->snd_una;
    cpl->last_bytes_acked = tp->bytes_acked;
    cpl->last_sacked_out = tp->sacked_out;

    cpl->skb_array = (struct skb_info*)kmalloc(MAX_SKB_STORED * sizeof(struct skb_info), GFP_KERNEL);
    if (!(cpl->skb_array)) {
        pr_info("[ccp] could not allocate skb array\n");
    }
    memset(cpl->skb_array, 0, MAX_SKB_STORED * sizeof(struct skb_info));

    cpl->conn = ccp_connection_start(kernel_datapath, (void *) sk, &dp_info);
    if (cpl->conn == NULL) {
        pr_info("[ccp] start connection failed\n");
    } else {
        pr_info("[ccp] starting connection %d", cpl->conn->index);
    }

    // if no ecn support
    if (!(tp->ecn_flags & TCP_ECN_OK)) {
        INET_ECN_dontxmit(sk);
    }
    
    cmpxchg(&sk->sk_pacing_status, SK_PACING_NONE, SK_PACING_NEEDED);
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    if (cpl->conn != NULL) {
        pr_info("[ccp] freeing connection %d", cpl->conn->index);
        ccp_connection_free(kernel_datapath, cpl->conn->index);
    } else {
        pr_info("[ccp] already freed");
    }
    if (cpl->skb_array != NULL) {
        kfree(cpl->skb_array);
        cpl->skb_array = NULL;
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

void ccp_log(struct ccp_datapath *dp, enum ccp_log_level level, const char* msg, int msg_size) {
    switch(level) {
    case ERROR:
    case WARN:
    case INFO:
    case DEBUG:
    case TRACE:
        pr_info("%s\n", msg);
        break;
    default:
        break;
    }
}

static int __init tcp_ccp_register(void) {
    int ok;

    ktime_get_real_ts64(&tzero);

#ifdef COMPAT_MODE
    pr_info("[ccp] Compatibility mode: 4.13 <= kernel version <= 4.16\n");
#endif
#ifdef RATESAMPLE_MODE
    pr_info("[ccp] Rate-sample mode: 4.19 <= kernel version\n");
#endif

    kernel_datapath = kmalloc(sizeof(struct ccp_datapath), GFP_KERNEL);
    if(!kernel_datapath) {
        pr_info("[ccp] could not allocate ccp_datapath\n");
        return -4;
    }

    kernel_datapath->max_connections = MAX_ACTIVE_FLOWS;
    kernel_datapath->ccp_active_connections =
        (struct ccp_connection *) kmalloc(sizeof(struct ccp_connection) * MAX_ACTIVE_FLOWS, GFP_KERNEL);
    if(!kernel_datapath->ccp_active_connections) {
        pr_info("[ccp] could not allocate ccp_active_connections\n");
        return -5;
    }

    kernel_datapath->max_programs = MAX_DATAPATH_PROGRAMS;
    kernel_datapath->set_cwnd = &do_set_cwnd;
    kernel_datapath->set_rate_abs = &do_set_rate_abs;
    kernel_datapath->now = &ccp_now;
    kernel_datapath->since_usecs = &ccp_since;
    kernel_datapath->after_usecs = &ccp_after;
    kernel_datapath->log = &ccp_log;
#if __IPC__ == IPC_NETLINK
    ok = ccp_nl_sk(&ccp_read_msg);
    if (ok < 0) {
        return -1;
    }

    kernel_datapath->send_msg = &nl_sendmsg;
    pr_info("[ccp] ipc = netlink\n");
#elif __IPC__ == IPC_CHARDEV
    ok = ccpkp_init(&ccp_read_msg);
    if (ok < 0) {
        return -2;
    }

    kernel_datapath->send_msg = &ccpkp_sendmsg;
    pr_info("[ccp] ipc = chardev\n");
#else
    pr_info("[ccp] ipc =  %s unknown\n", __IPC__);
    return -3;
#endif
	
    ok = ccp_init(kernel_datapath);
    if (ok < 0) {
        return -6;
    }

    pr_info("[ccp] init\n");
    return tcp_register_congestion_control(&tcp_ccp_congestion_ops);
}

static void __exit tcp_ccp_unregister(void) {
    tcp_unregister_congestion_control(&tcp_ccp_congestion_ops);
#if __IPC__ == IPC_NETLINK
    free_ccp_nl_sk();
#elif __IPC__ == IPC_CHARDEV
    ccpkp_cleanup();
#endif
    kfree(kernel_datapath->ccp_active_connections);
    kfree(kernel_datapath);
    pr_info("[ccp] exit\n");
}

module_init(tcp_ccp_register);
module_exit(tcp_ccp_unregister);

MODULE_AUTHOR("Akshay Narayan <akshayn@mit.edu>");
MODULE_DESCRIPTION("Kernel datapath for a congestion control plane");
MODULE_LICENSE("GPL");
