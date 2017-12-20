#ifndef TCP_CCP_H
#define TCP_CCP_H

#include <linux/net.h>
#include "libccp/common_headers.h"
/* -----------------------------------------------------------------------------
 * CCP TCP state
 * -----------------------------------------------------------------------------
 */

struct ccp {
    // control
    u32 rate; // 4 B
    enum drop_type last_drop_state; // 1 b

    // measurement
    struct ccp_primitives mmt; // 40 B

    // communication
    struct ccp_connection *dp;
};

#define MTU 1500
#define S_TO_US 1000000

void ccp_set_pacing_rate(struct sock *sk);


#endif
