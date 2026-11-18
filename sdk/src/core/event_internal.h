#ifndef DINO_CORE_EVENT_INTERNAL_H
#define DINO_CORE_EVENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <dino/core/event.h>
#include <dino/core/mutex.h>

#define DINO__EVENT_INTEREST (DINO_EVENT_READ | DINO_EVENT_WRITE | DINO_EVENT_ONESHOT)

static inline int dino__event_wait_args_valid(
    dino_event_loop_t loop, const dino_event_t *out, int max)
{
    return loop != DINO_INVALID_EVENT_LOOP && out != NULL && max > 0;
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
    dino_mutex_t               lock;
    struct dino__event_timer **heap;
    size_t                     len, cap;
    struct dino__event_timer  *all; // every timer, fired one-shots included
};

int  dino__event_timers_init(struct dino__event_timers *t);
void dino__event_timers_destroy(struct dino__event_timers *t);

// timeout_ms (-1 for none) shortened to the next deadline, rounded up so the
// timer is due when the wait returns.
int dino__event_timers_timeout(struct dino__event_timers *t, int timeout_ms);

// Writes up to max due timers to out and reschedules the repeating ones.
int dino__event_timers_collect(struct dino__event_timers *t, dino_event_t *out, int max);

// Makes a blocked dino_event_wait recompute its timeout, without reporting a
// DINO_EVENT_WAKE. Each platform implements it.
void dino__event_interrupt(dino_event_loop_t loop);

// Milliseconds left until deadline_ns (0 when past), or -1 when timeout_ms was -1.
int dino__event_remaining_ms(int timeout_ms, uint64_t deadline_ns);

#endif // DINO_CORE_EVENT_INTERNAL_H
