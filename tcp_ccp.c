#include <linux/module.h>
#include <net/tcp.h>

#include "ccp_nl.h"

struct ccp {
    uint16_t ccp_index;
    struct sock *nl_sk;
    u32 beg_snd_una; /* left edge during last RTT */
};

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

/*
 * TCP Reno congestion control
 * This is special case used for fallback as well.
 */
/* This is Jacobson's slow start and congestion avoidance.
 * SIGCOMM '88, p. 328.
 */
/*
 * ack = the cumulative ack
 * acked = the delta between this ack and the prev cumAck
 */
void tcp_ccp_cong_avoid(struct sock *sk, u32 ack, u32 acked) {
    struct ccp *cpl;
    struct tcp_sock *tp = tcp_sk(sk);

    if (!tcp_is_cwnd_limited(sk)) {
        //printk(KERN_INFO "not cwnd limited: %d\n", tp->snd_cwnd);
        return;
    }

    /* In "safe" area, increase. */
    //if (tcp_in_slow_start(tp)) {
    //    acked = tcp_slow_start(tp, acked);
    //    if (!acked)
    //        return;
    //}
    /* In dangerous area, increase slowly. */
    //tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);

    cpl = inet_csk_ca(sk);
    // if an RTT has passed
    if (after(ack, cpl->beg_snd_una)) {
        cpl->beg_snd_una = tp->snd_nxt;
        //printk(KERN_INFO "sending ack notif: cumAck=%u, srtt=%u, snd_una=%u, snd_cwnd=%u\n", ack, tp->srtt_us, cpl->beg_snd_una, tp->snd_cwnd);
        nl_send_ack_notif(cpl->nl_sk, cpl->ccp_index, ack, tp->srtt_us);
    } else {
        //printk(KERN_INFO "ack: cumAck=%u, snd_una=%u, cwnd=%u\n", ack, cpl->beg_snd_una, tp->snd_cwnd);
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_cong_avoid);

void tcp_ccp_init(struct sock *sk) {
    struct tcp_sock *tp;
    struct sock *nl_sk;
    struct ccp *cpl;
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv_cwnd,
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
    cpl->beg_snd_una = tp->snd_una;

    // send to CCP:
    // index of pointer back to this sock for IPC callback
    // first ack to expect
    nl_send_conn_create(nl_sk, cpl->ccp_index, tp->snd_una);
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
    .cong_avoid = tcp_ccp_cong_avoid,
    .undo_cwnd = tcp_ccp_undo_cwnd,
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
