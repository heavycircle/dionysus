#include <dino/core/event.h>

#include <stdlib.h>

#include <dino/core/time.h>

#include "error_internal.h"
#include "event_internal.h"

#define NOT_IN_HEAP SIZE_MAX

struct dino__event_timer
{
    uint64_t                   deadline_ns;
    uint64_t                   interval_ns;
    void                      *userdata;
    size_t                     index; // in the heap, or NOT_IN_HEAP once a one-shot fired
    struct dino__event_timers *owner; // deleting through another loop would corrupt both
    struct dino__event_timer  *prev, *next;
};

int dino__event_timers_init(struct dino__event_timers *t)
{
    *t      = (struct dino__event_timers) { 0 };
    t->lock = dino_mutex_create();
    return t->lock == DINO_INVALID_MUTEX ? -1 : 0;
}

void dino__event_timers_destroy(struct dino__event_timers *t)
{
    for (struct dino__event_timer *tm = t->all, *next; tm; tm = next)
    {
        next = tm->next;
        free(tm);
    }
    free((void *)t->heap);
    dino_mutex_destroy(t->lock);
}

static void place(struct dino__event_timers *t, size_t i, struct dino__event_timer *tm)
{
    t->heap[i] = tm;
    tm->index  = i;
}

static void sift_up(struct dino__event_timers *t, size_t i)
{
    struct dino__event_timer *tm = t->heap[i];
    while (i > 0)
    {
        size_t parent = (i - 1) / 2;
        if (t->heap[parent]->deadline_ns <= tm->deadline_ns)
            break;
        place(t, i, t->heap[parent]);
        i = parent;
    }
    place(t, i, tm);
}

static void sift_down(struct dino__event_timers *t, size_t i)
{
    struct dino__event_timer *tm = t->heap[i];
    for (;;)
    {
        size_t child = 2 * i + 1;
        if (child >= t->len)
            break;
        if (child + 1 < t->len && t->heap[child + 1]->deadline_ns < t->heap[child]->deadline_ns)
            child++;
        if (tm->deadline_ns <= t->heap[child]->deadline_ns)
            break;
        place(t, i, t->heap[child]);
        i = child;
    }
    place(t, i, tm);
}

static int push(struct dino__event_timers *t, struct dino__event_timer *tm)
{
    if (t->len == t->cap)
    {
        size_t                     cap = t->cap ? t->cap * 2 : 8;
        struct dino__event_timer **heap
            = (struct dino__event_timer **)realloc((void *)t->heap, cap * sizeof(*heap));
        if (!heap)
            return -1;
        t->heap = heap;
        t->cap  = cap;
    }
    place(t, t->len++, tm);
    sift_up(t, tm->index);
    return 0;
}

static void remove_at(struct dino__event_timers *t, size_t i)
{
    t->heap[i]->index = NOT_IN_HEAP;
    if (--t->len == i)
        return;
    place(t, i, t->heap[t->len]);
    sift_down(t, i);
    sift_up(t, t->heap[i]->index);
}

dino_event_timer_t dino_event_timer_add(
    dino_event_loop_t loop, uint32_t delay_ms, uint32_t interval_ms, void *userdata)
{
    if (loop == DINO_INVALID_EVENT_LOOP)
    {
        dino__error_set(DINO_EINVAL);
        return DINO_INVALID_EVENT_TIMER;
    }
    struct dino__event_timers *t  = (struct dino__event_timers *)loop;
    struct dino__event_timer  *tm = calloc(1, sizeof(*tm));
    if (!tm)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_EVENT_TIMER;
    }
    tm->deadline_ns = dino_time_monotonic_ns() + (uint64_t)delay_ms * 1000000;
    tm->interval_ns = (uint64_t)interval_ms * 1000000;
    tm->userdata    = userdata;
    tm->owner       = t;

    dino_mutex_lock(t->lock);
    int ok = push(t, tm) == 0;
    if (ok)
    {
        tm->next = t->all;
        if (t->all)
            t->all->prev = tm;
        t->all = tm;
    }
    int earliest = ok && tm->index == 0;
    dino_mutex_unlock(t->lock);

    if (!ok)
    {
        free(tm);
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_EVENT_TIMER;
    }
    // A thread already waiting computed its timeout from the old earliest.
    if (earliest)
        dino__event_interrupt(loop);
    return (dino_event_timer_t)tm;
}

int dino_event_timer_del(dino_event_loop_t loop, dino_event_timer_t timer)
{
    if (loop == DINO_INVALID_EVENT_LOOP || timer == DINO_INVALID_EVENT_TIMER)
        return dino__fail(DINO_EINVAL);
    struct dino__event_timers *t  = (struct dino__event_timers *)loop;
    struct dino__event_timer  *tm = (struct dino__event_timer *)timer;
    if (tm->owner != t)
        return dino__fail(DINO_EINVAL);
    dino_mutex_lock(t->lock);
    if (tm->index != NOT_IN_HEAP)
        remove_at(t, tm->index);
    if (tm->prev)
        tm->prev->next = tm->next;
    else
        t->all = tm->next;
    if (tm->next)
        tm->next->prev = tm->prev;
    dino_mutex_unlock(t->lock);
    free(tm);
    return 0;
}

int dino__event_remaining_ms(int timeout_ms, uint64_t deadline_ns)
{
    if (timeout_ms < 0)
        return -1;
    uint64_t now = dino_time_monotonic_ns();
    if (now >= deadline_ns)
        return 0;
    uint64_t ms = (deadline_ns - now + 999999) / 1000000;
    return ms > INT32_MAX ? INT32_MAX : (int)ms;
}

int dino__event_timers_timeout(struct dino__event_timers *t, int timeout_ms)
{
    dino_mutex_lock(t->lock);
    int next = t->len ? dino__event_remaining_ms(0, t->heap[0]->deadline_ns) : -1;
    dino_mutex_unlock(t->lock);
    if (next < 0)
        return timeout_ms;
    return timeout_ms < 0 || next < timeout_ms ? next : timeout_ms;
}

int dino__event_timers_turn(struct dino__event_timers *t)
{
    dino_mutex_lock(t->lock);
    int first       = t->timers_first;
    t->timers_first = !first;
    dino_mutex_unlock(t->lock);
    return first;
}

int dino__event_timers_collect(struct dino__event_timers *t, dino_event_t *out, int max)
{
    int n = 0;
    dino_mutex_lock(t->lock);
    uint64_t now = dino_time_monotonic_ns();
    while (n < max && t->len && t->heap[0]->deadline_ns <= now)
    {
        struct dino__event_timer *tm = t->heap[0];
        out[n].events                = DINO_EVENT_TIMER;
        out[n].userdata              = tm->userdata;
        n++;
        if (tm->interval_ns)
        {
            // Next period after now: missed periods are skipped, not replayed.
            tm->deadline_ns += tm->interval_ns * (1 + (now - tm->deadline_ns) / tm->interval_ns);
            sift_down(t, 0);
        }
        else
        {
            remove_at(t, 0);
        }
    }
    dino_mutex_unlock(t->lock);
    return n;
}
