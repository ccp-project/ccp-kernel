#ifndef TCP_CCP_H
#define TCP_CCP_H

#include <linux/net.h>
#include <linux/tcp.h>
#include "libccp/ccp.h"

struct ccp {
    // control
    u32 rate; // 4 B

    // measurement
    struct ccp_primitives mmt; // 40 B

    // communication
    struct ccp_connection *dp;
};

#define MTU 1500
#define S_TO_US 1000000

void ccp_set_pacing_rate(struct sock *sk);

#endif
