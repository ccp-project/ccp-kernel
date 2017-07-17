#ifndef CCP_NL_H
#define CCP_NL_H

#include <net/sock.h>
#include "tcp_ccp.h"

// CCP connection lookup
struct ccp_connection {
    uint16_t index;
    struct sock *sk;
};

int ccp_init_connection_map(void);
void ccp_free_connection_map(void);
uint16_t ccp_connection_start(struct sock *sk);
void ccp_connection_free(uint16_t sid);
struct sock *ccp_connection_lookup(uint16_t sid);

void check_nlsk_created(
    struct ccp *cpl,
    u32 una
);

void nl_recv(struct sk_buff *skb);
int nl_send_conn_create(
    struct sock *nl_sk, 
    uint16_t ccp_index, 
    uint32_t startSeq
);

void nl_send_measurement(
    struct sock *nl_sk, 
    uint16_t ccp_index, 
    struct ccp_measurement mmt
);

int nl_send_drop_notif(
    struct sock *nl_sl,
    uint16_t ccp_index,
    enum drop_type dtype
);

#endif
