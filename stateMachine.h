#ifndef CCP_STATEMACHINE_H
#define CCP_STATEMACHINE_H

#include <linux/net.h>
#include <net/tcp.h>
#include "serialize.h"

/* cwnd in bytes
 */
void doSetCwndAbs(
    struct tcp_sock *tp, 
    uint32_t cwnd
);

/* rate in Bytes/sec
 */
void doSetRateAbs(
    struct sock *sk,
    uint32_t rate
); 

void doSetRateRel(
    struct sock *sk,
    uint32_t factor
);

void doReport(
    struct sock *sk
);

void sendStateMachine(
    struct sock *sk
);

void installPattern(
    struct sock *sk,
    int numEvents,
    struct PatternEvent *seq
);

#endif
