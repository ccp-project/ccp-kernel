#ifndef CCP_NL_H
#define CCP_NL_H

#include <net/sock.h>

void nl_recv_cwnd(struct sk_buff *skb);
void nl_send_ack_notif(
    struct sock *nl_sk, 
    struct sock *sk, 
    u32 cumAck, 
    u32 srtt,
);

#endif
