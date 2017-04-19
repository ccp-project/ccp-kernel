#include <linux/net.h>

#include "ccp_nl.h"
#include "serialize.h"

#define CCP_MULTICAST_GROUP 22

// write map of array index -> struct sock to manip
// init from tcp_ccp_init
// cleanup in .release
// lookup in nl_recv_cwnd to manipulate appropriate thing

void nl_recv_cwnd(struct sk_buff *skb) {
    struct nlmsghdr *nlh = nlmsg_hdr(skb);
    int msg_size;
    struct UInt32AndUInt32 cwndMsg;

    printk(KERN_INFO "Entering %s\n", __FUNCTION__);

    printk(KERN_INFO "Netlink raw rcvd:\n");
    msg_size = readCwndMsg((char*)nlmsg_data(nlh), &cwndMsg);
    printk(KERN_INFO "Netlink received msg payload: (%d, %d)\n", cwndMsg.Val1, cwndMsg.Val2);
}

void nl_send_ack_notif(struct sock *nl_sk, struct sock *sk, u32 cumAck, u32 srtt) {
    int res;
    int sockid;
    struct sk_buff *skb_out;
    struct nlmsghdr *nlh;
    char msg[MAX_STRING_SIZE+6];
    int msg_size;

    //printk(KERN_INFO "Entering %s\n", __FUNCTION__);
    
    sockid = sk->sk_socket->file;

    msg_size = writeAckMsg(msg, sockid, cumAck, srtt);

    skb_out = nlmsg_new(
        msg_size, // @payload: size of the message payload
        0         // @flags: the type of memory to allocate.
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

    memcpy(nlmsg_data(nlh), msg, msg_size);
    res = nlmsg_multicast(
        nl_sk,               // @sk: netlink socket to spread messages to
        skb_out,             // @skb: netlink message as socket buffer
        0,                   // @portid: own netlink portid to avoid sending to yourself
        CCP_MULTICAST_GROUP, // @group: multicast group id
        GFP_KERNEL           // @flags: allocation flags
    );
    if (res < 0) {
        /* Wait 1 second. */
        //mod_timer(&timer, jiffies + msecs_to_jiffies(1000));
    }
}
