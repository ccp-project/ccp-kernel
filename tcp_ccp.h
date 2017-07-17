#ifndef TCP_CCP_H
#define TCP_CCP_H

#include <linux/net.h>

#define MTU 1500
#define S_TO_US 100000

struct ccp_measurement {
    u32 ack;
    u32 rtt;
    u64 rin;
    u64 rout;
};

enum drop_type {
    NO_DROP,
    DROP_TIMEOUT,
    DROP_DUPACK,
    DROP_ECN
};

#define SETRATEABS 0 
#define SETCWNDABS 1 
#define SETRATEREL 2 
#define WAITABS    3
#define WAITREL    4
#define REPORT     5

struct __attribute__((packed, aligned(2))) PatternEvent {
    uint8_t type;
    uint8_t size;
    uint32_t val;
};

struct ccp {
    // control
    u32 rate;
    struct PatternEvent *pattern;
    uint8_t currPatternEvent;
    uint8_t numPatternEvents;
    u32 next_event_time;
    enum drop_type last_drop_state;

    // measurement
    struct ccp_measurement mmt;

    // communication
    bool created;
    uint16_t ccp_index;
    struct sock *nl_sk;
};

void ccp_set_pacing_rate(struct sock *sk);

#endif
