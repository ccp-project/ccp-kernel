#include <net/tcp.h>

#include "stateMachine.h"
#include "tcp_ccp.h"
#include "ccp_nl.h"

void doSetCwndAbs(
    struct tcp_sock *tp, 
    uint32_t cwnd
) {
    // translate cwnd value back into packets
    cwnd /= tp->mss_cache;
    printk(KERN_INFO "cwnd %d -> %d (mss %d)\n", tp->snd_cwnd, cwnd, tp->mss_cache);
    tp->snd_cwnd = cwnd;
}

void doSetRateAbs(
    struct sock *sk,
    uint32_t rate
) {
    struct ccp *ca = inet_csk_ca(sk);

    // rate is * 100
    do_div(rate, 100);
    printk(KERN_INFO "rate -> %d\n", rate);
    ca->rate = rate;
    ccp_set_pacing_rate(sk);
}

void doSetRateRel(
    struct sock *sk,
    uint32_t factor
) {
    struct ccp *ca = inet_csk_ca(sk);

    // factor is * 100
    uint64_t newrate = ca->rate * factor;
    do_div(newrate, 100);
    printk(KERN_INFO "rate -> %llu\n", newrate);
    ca->rate = (u32) newrate;
    ccp_set_pacing_rate(sk);
}

void doReport(
    struct sock *sk
) {
    struct ccp *cpl = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp_measurement mmt = cpl->mmt;
    check_nlsk_created(cpl, tp->snd_una);
    nl_send_measurement(cpl->nl_sk, cpl->ccp_index, mmt);
}

void doWaitAbs(
    struct sock *sk,
    uint32_t wait_us
) {
    struct ccp *cpl = inet_csk_ca(sk);
    do_div(wait_us, 1000);
    cpl->next_event_time = tcp_time_stamp + msecs_to_jiffies(wait_us);
}

void doWaitRel(
    struct sock *sk,
    uint32_t rtt_factor
) {
    struct ccp *cpl = inet_csk_ca(sk);
    u64 rtt_us = cpl->mmt.rtt;
    u64 wait_us = rtt_factor * rtt_us;
    do_div(wait_us, 1000);
    cpl->next_event_time = tcp_time_stamp + msecs_to_jiffies(wait_us);
}

void sendStateMachine(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    struct PatternEvent ev;
    if (cpl->numPatternEvents == 0) {
        return;
    }

    if (unlikely(after(tcp_time_stamp, cpl->next_event_time))) {
        cpl->currPatternEvent = (cpl->currPatternEvent + 1) % cpl->numPatternEvents;
    } else {
        return;
    }

    ev = cpl->pattern[cpl->currPatternEvent];
    switch (ev.type) {
    case SETRATEABS:
        doSetRateAbs(sk, ev.val);
        break;
    case SETCWNDABS:
        doSetCwndAbs(tp, ev.val);
        break;
    case SETRATEREL:
        doSetRateRel(sk, ev.val);
        break;
    case WAITREL:
        doWaitRel(sk, ev.val);
        break;
    case WAITABS:
        doWaitAbs(sk, ev.val);
        break;
    case REPORT:
        doReport(sk);
        break;
    }
}

void installPattern(
    struct sock *sk,
    int numEvents,
    struct PatternEvent *seq
) {
    struct ccp *ca = inet_csk_ca(sk);
    ca->numPatternEvents = numEvents;
    ca->currPatternEvent = 0;
    ca->pattern = seq;
}
