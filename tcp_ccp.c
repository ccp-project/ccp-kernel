#include <linux/module.h>
#include <net/tcp.h>

#include "ccp_nl.h"

struct ccp {
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

    if (!tcp_is_cwnd_limited(sk))
        return;

    /* In "safe" area, increase. */
    if (tcp_in_slow_start(tp)) {
        acked = tcp_slow_start(tp, acked);
        if (!acked)
            return;
    }
    /* In dangerous area, increase slowly. */
    tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);

    cpl = inet_csk_ca(sk);

    // if an RTT has passed
    if (after(ack, cpl->beg_snd_una)) {
        cpl->beg_snd_una = tp->snd_nxt;
        printk(KERN_INFO "sending ack notif: cumAck=%d, srtt=%d, snd_una=%d\n", ack, tp->srtt_us, cpl->beg_snd_una);

        nl_send_ack_notif(cpl->nl_sk, sk, ack, tp->srtt_us);
    }
}
EXPORT_SYMBOL_GPL(tcp_ccp_cong_avoid);

void tcp_ccp_init(struct sock *sk) {
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
    cpl = inet_csk_ca(sk);
    cpl->nl_sk = nl_sk;

    cpl->beg_snd_una = 1;
}
EXPORT_SYMBOL_GPL(tcp_ccp_init);

void tcp_ccp_release(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    printk(KERN_INFO "exit NL\n");
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
    printk(KERN_INFO "Init ccp\n");
    return tcp_register_congestion_control(&tcp_ccp_congestion_ops);
}

static void __exit tcp_ccp_unregister(void) {
    printk(KERN_INFO "Exit ccp\n");
    tcp_unregister_congestion_control(&tcp_ccp_congestion_ops);
}

module_init(tcp_ccp_register);
module_exit(tcp_ccp_unregister);

MODULE_AUTHOR("Akshay Narayan <akshayn@mit.edu>");
MODULE_DESCRIPTION("Kernel datapath for a congestion control plane");
MODULE_LICENSE("GPL");
