#include <dino/core/time.h>

#include "time_internal.h"

// Its own block: <time.h> would otherwise count as this file's main header.
#include <errno.h>
#include <time.h>

uint64_t dino_time_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_S + (uint64_t)ts.tv_nsec;
}

// An absolute deadline makes resuming after EINTR exact: a relative nanosleep
// would drift by however long the signal handler ran.
void dino__sleep_ns(uint64_t ns)
{
    if (ns == 0)
        return;
    uint64_t now      = dino_time_monotonic_ns();
    uint64_t deadline = ns > UINT64_MAX - now ? UINT64_MAX : now + ns;

    struct timespec ts;
    ts.tv_sec  = (time_t)(deadline / NS_PER_S);
    ts.tv_nsec = (long)(deadline % NS_PER_S);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}
