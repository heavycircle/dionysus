#ifndef DINO_CORE_EVENT_INTERNAL_H
#define DINO_CORE_EVENT_INTERNAL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <dino/core/event.h>

#include "mutex_internal.h"

#define DINO__EVENT_INTEREST (DINO_EVENT_READ | DINO_EVENT_WRITE | DINO_EVENT_ONESHOT)

static inline int dino__event_wait_args_valid(
    dino_event_loop_t loop, const dino_event_t *out, int max)
{
    return loop != DINO_INVALID_EVENT_LOOP && out != NULL && max > 0;
}

static inline int dino__event_pipe_args_valid(dino_event_loop_t loop, uint32_t events)
{
    return loop != DINO_INVALID_EVENT_LOOP && (events & DINO_EVENT_READ)
        && (events & ~(DINO_EVENT_READ | DINO_EVENT_ONESHOT)) == 0;
}

static inline int dino__event_ctl_args_valid(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events)
{
    return loop != DINO_INVALID_EVENT_LOOP && sock != DINO_INVALID_SOCKET
        && (events & ~DINO__EVENT_INTEREST) == 0;
}

// Timers are kept the same way on every platform (event.c): a min-heap on
// deadline, so a wait only has to know the earliest one. Every platform's
// struct dino_event_loop starts with this, so event.c reaches it from the
// loop handle.
struct dino__event_timers
{
    dino__mutex_t              lock;
    struct dino__event_timer **heap;
    size_t                     len, cap;
    struct dino__event_timer  *all;          // every timer, fired one-shots included
    atomic_int                 timers_first; // flips every wait: see dino__event_timers_turn
    // Threads blocked on a timeout computed from the heap: only they need
    // interrupting when an earlier timer is added. Raised under lock.
    atomic_int waiting;
};

int  dino__event_timers_init(struct dino__event_timers *t);
void dino__event_timers_destroy(struct dino__event_timers *t);

// Whether this wait collects due timers before the kernel queue. Waits take
// turns, so a socket that stays ready can't starve the timers, nor a timer
// that is always due the sockets, even with max 1.
int dino__event_timers_turn(struct dino__event_timers *t);

// One step of a wait over the timers, under one lock and one clock read:
// writes up to max due timers to out (max 0: none) and reschedules the
// repeating ones. *left_ms gets what remains of a wait of timeout_ms that
// ends at deadline_ns (-1: no limit). With wait_ms, *wait_ms gets that
// shortened to the next deadline, rounded up so the timer is due when the
// wait returns; if no timer was due the caller is about to block on it, and
// counts as waiting until it calls dino__event_timers_waited.
int  dino__event_timers_step(struct dino__event_timers *t, dino_event_t *out, int max,
     int timeout_ms, uint64_t deadline_ns, int *left_ms, int *wait_ms);
void dino__event_timers_waited(struct dino__event_timers *t);

// Makes a blocked dino_event_wait recompute its timeout, without reporting a
// DINO_EVENT_WAKE. Each platform implements it.
void dino__event_interrupt(dino_event_loop_t loop);

// Milliseconds from now_ns until deadline_ns (0 when past), or -1 when
// timeout_ms was -1.
int dino__event_remaining_ms(int timeout_ms, uint64_t deadline_ns, uint64_t now_ns);

#endif // DINO_CORE_EVENT_INTERNAL_H
