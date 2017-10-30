#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <net/sock.h>

#include "tcp_ccp.h"

#define MYMGRP 22

struct sock *nl_sk = NULL;
//static struct timer_list timer;

static void log_msg(char *msg, int msg_size) {
    size_t i;
    char pt[msg_size * 6];
    for (i = 0; i < msg_size; i++) {
        printk(KERN_INFO "msg byte: %lu, %02hhX\n", i, msg[i]);
        snprintf(pt+i*6, 6, "0x%02hhX, ", msg[i]);
    }

    printk(KERN_INFO "msg: [%s]\n", pt);
}

static void nl_recv_msg(struct sk_buff *skb) {
    int pid; // pid of sending process
    int res;
    size_t i;
    //struct sk_buff *skb_out;
    struct nlmsghdr *nlh = nlmsg_hdr(skb);
    //char msg[30];
    int msg_size;
    struct CcpMsgHeader hdr;
    struct PatternMsg rmsg;
    struct PatternEvent *sequence;
    struct PatternEvent ev;

    printk(KERN_INFO "Entering %s\n", __FUNCTION__);

    printk(KERN_INFO "Netlink raw rcvd:\n");
    msg_size = readMsg(&hdr, &rmsg, (char*)nlmsg_data(nlh));
    if (msg_size < 0) {
        printk(KERN_INFO "bad message rcvd\n");
        return;
    }

    pr_info("size %d\n", msg_size);
    log_msg((char*)nlmsg_data(nlh), msg_size);
    printk(KERN_INFO "Netlink received msg payload: (%d, %d)\n", hdr.SocketId, rmsg.numStates);
    pid = nlh->nlmsg_pid;

    sequence = kmalloc(rmsg.numStates * sizeof(struct PatternEvent), GFP_KERNEL);
    if (!sequence) {
        return;
    }
    
    res = readPattern(sequence, rmsg.pattern, rmsg.numStates);
    if (res < 0) {
        return;
    }

    pr_info("got pattern with %d states\n", rmsg.numStates);
    for (i = 0; i < rmsg.numStates; i++) {
        ev = sequence[i];
        switch (ev.type) {
        case SETRATEABS:
            pr_info("Rate_Abs size %d, val %d\n", ev.size, ev.val);
            break;
        case SETCWNDABS:
            pr_info("Cwnd_Abs size %d, val %d\n", ev.size, ev.val);
            break;
        case SETRATEREL:
            pr_info("Rate_Rel size %d, val %d\n", ev.size, ev.val);
            break;
        case WAITABS:
            pr_info("Wait_Abs size %d, val %d (us)\n", ev.size, ev.val);
            break;
        case WAITREL:
            pr_info("Wait_Rel size %d, val %d\n", ev.size, ev.val);
            break;
        case REPORT:
            pr_info("Report, size %d\n", ev.size);
            break;
        }
    }

    //msg_size = writeMeasureMsg(msg, 30, 42, 1461, 255, 0, 0, 0);
    //pr_info("msg_size: %d\n", msg_size);
    //log_msg(msg, msg_size);

    //skb_out = nlmsg_new(
    //    msg_size, // @payload: size of the message payload
    //    0         // @flags: the type of memory to allocate.
    //);
    //if (!skb_out) {
    //    printk(KERN_ERR "Failed to allocate new skb\n");
    //    return;
    //}

    //nlh = nlmsg_put(
    //    skb_out,    // @skb: socket buffer to store message in
    //    0,          // @portid: netlink PORTID of requesting application
    //    0,          // @seq: sequence number of message
    //    NLMSG_DONE, // @type: message type
    //    msg_size,   // @payload: length of message payload
    //    0           // @flags: message flags
    //);

    //NETLINK_CB(skb_out).dst_group = 0;
    //memcpy(nlmsg_data(nlh), msg, msg_size);
    //res = nlmsg_unicast(nl_sk, skb_out, pid);
    //if (res < 0) {
    //    printk(KERN_INFO "Error while sending back to user\n");
    //}
}

void nl_send_msg(unsigned long data) {
    struct sk_buff *skb_out;
    struct nlmsghdr *nlh;
    int res;
    char msg[15];
    int msg_size;

    msg_size = writeCreateMsg(msg, 15, 42, 0, "reno");
    pr_info("msg_size: %d\n", msg_size);
    log_msg(msg, msg_size);

    skb_out = nlmsg_new(
        NLMSG_ALIGN(msg_size), // @payload: size of the message payload
        GFP_KERNEL             // @flags: the type of memory to allocate.
    );
    if (!skb_out) {
        printk(KERN_ERR "Failed to allocate new skb\n");
        return;
    }

    nlh = nlmsg_put(
        skb_out,    // @skb: socket buffer to store message in
        0,          // @portid: netlink PORTID of requesting application
        0,          // @seq: sequence number of message
        NLMSG_DONE, // @type: message type
        msg_size,   // @payload: length of message payload
        0           // @flags: message flags
    );

    //NETLINK_CB(skb_out).dst_group = 0;
    memcpy(nlmsg_data(nlh), msg, msg_size);
    printk(KERN_INFO "Sending proactive kernel message\n");
    res = nlmsg_multicast(
            nl_sk,     // @sk: netlink socket to spread messages to
            skb_out,   // @skb: netlink message as socket buffer
            0,         // @portid: own netlink portid to avoid sending to yourself
            MYMGRP,    // @group: multicast group id
            GFP_KERNEL // @flags: allocation flags
    );
    if (res < 0) {
        printk(KERN_INFO "Error while sending to user: %d\n", res);
        /* Wait 1 second. */
        //mod_timer(&timer, jiffies + msecs_to_jiffies(1000));
    } else {
        printk(KERN_INFO "Send ok\n");
    }
}

static int __init nl_init(void) {
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv_msg,
    };
    
    printk(KERN_INFO "init NL\n");

    nl_sk = netlink_kernel_create(&init_net, NETLINK_USERSOCK, &cfg);
    if (!nl_sk) {
        printk(KERN_ALERT "Error creating socket.\n");
        return -10;
    }

    //init_timer(&timer);
    //timer.function = nl_send_msg;
    //timer.expires = jiffies + 1000;
    //timer.data = 0;
    //add_timer(&timer);
    
    nl_send_msg(0);

    return 0;
}

static void __exit nl_exit(void) {
    printk(KERN_INFO "exit NL\n");
    //del_timer_sync(&timer);
    netlink_kernel_release(nl_sk);
}

module_init(nl_init);
module_exit(nl_exit);

MODULE_LICENSE("GPL");
