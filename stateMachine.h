#ifndef CCP_STATEMACHINE_H
#define CCP_STATEMACHINE_H

#include <linux/net.h>
#include <net/tcp.h>
#include "serialize.h"

void sendStateMachine(
    struct sock *sk
);

void installPattern(
    struct sock *sk,
    int numEvents,
    struct PatternEvent *seq
);

#endif
