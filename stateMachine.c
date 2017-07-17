#include <net/tcp.h>

#include "stateMachine.h"
#include "tcp_ccp.h"
#include "ccp_nl.h"

static void doSetCwndAbs(
    struct tcp_sock *tp, 
    uint32_t cwnd
) {
    // translate cwnd value back into packets
    cwnd /= tp->mss_cache;
    printk(KERN_INFO "cwnd %d -> %d (mss %d)\n", tp->snd_cwnd, cwnd, tp->mss_cache);
    tp->snd_cwnd = cwnd;
}

static void doSetRateAbs(
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

static void doSetRateRel(
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

static void doReport(
    struct sock *sk
) {
    struct ccp *cpl = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    struct ccp_measurement mmt = cpl->mmt;
    pr_info("sending report\n");
    check_nlsk_created(cpl, tp->snd_una);
    nl_send_measurement(cpl->nl_sk, cpl->ccp_index, mmt);
}

static void doWaitAbs(
    struct sock *sk,
    uint32_t wait_us
) {
    struct ccp *cpl = inet_csk_ca(sk);
    do_div(wait_us, 100);
    pr_info("waiting %u us\n", wait_us);
    cpl->next_event_time = tcp_time_stamp + msecs_to_jiffies(wait_us);
}

static void doWaitRel(
    struct sock *sk,
    uint32_t rtt_factor
) {
    struct ccp *cpl = inet_csk_ca(sk);
    u64 rtt_us = cpl->mmt.rtt;
    u64 wait_us = rtt_factor * rtt_us;
    do_div(wait_us, 100);
    pr_info("waiting %llu us (%u/100 rtts) (rtt = %llu us)\n", wait_us, rtt_factor, rtt_us);
    cpl->next_event_time = tcp_time_stamp + msecs_to_jiffies(wait_us);
}

void sendStateMachine(struct sock *sk) {
    struct ccp *cpl = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    struct PatternEvent ev;
    if (cpl->numPatternEvents == 0) {
        pr_info("empty pattern\n");
        return;
    }

    if (unlikely(after(tcp_time_stamp, cpl->next_event_time))) {
        cpl->currPatternEvent = (cpl->currPatternEvent + 1) % cpl->numPatternEvents;
        pr_info("curr pattern event: %d\n", cpl->currPatternEvent);
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

static void log_sequence(struct PatternEvent *seq, int numEvents) {
    size_t  i;
    struct PatternEvent ev;
    pr_info("installed pattern:\n");
    for (i = 0; i < numEvents; i++) {
        ev = seq[i];
        switch (ev.type) {
        case SETRATEABS:
            break;
        case SETCWNDABS:
            pr_info("[ev %lu] set cwnd %d\n", i, ev.val);
            break;
        case SETRATEREL:
            break;
        case WAITREL:
            pr_info("[ev %lu] wait rtts %d/100\n", i, ev.val);
            break;
        case WAITABS:
            pr_info("[ev %lu] wait %d us\n", i, ev.val);
            break;
        case REPORT:
            pr_info("[ev %lu] send report\n", i);
            break;
        }
    }
}

void installPattern(
    struct sock *sk,
    int numEvents,
    struct PatternEvent *seq
) {
    struct ccp *ca = inet_csk_ca(sk);

    log_sequence(seq, numEvents);

    ca->numPatternEvents = numEvents;
    ca->currPatternEvent = numEvents - 1;
    ca->next_event_time = tcp_time_stamp;
    ca->pattern = seq;

    sendStateMachine(sk);
}
