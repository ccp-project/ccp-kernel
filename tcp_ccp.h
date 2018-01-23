#ifndef TCP_CCP_H
#define TCP_CCP_H

#include <linux/net.h>
#include <linux/tcp.h>
#include "libccp/ccp.h"

struct ccp {
    // control
    u32 last_snd_una; // 4 B
    u32 last_bytes_acked; // 8 B
    u32 last_sacked_out; // 12 B

    // communication
    struct ccp_connection *dp;
};

#define MTU 1500
#define S_TO_US 1000000

void ccp_set_pacing_rate(struct sock *sk, uint32_t rate);

#endif
