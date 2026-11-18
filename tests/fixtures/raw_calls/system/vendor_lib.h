// Stands in for a third-party header-only library included as a system header:
// its inline functions call flagged APIs, which the project can't change, so
// they must not warn.
#ifndef VENDOR_LIB_H
#define VENDOR_LIB_H

#pragma GCC system_header

#include <pthread.h>
#include <unistd.h>

static inline int vendor_lock(pthread_mutex_t *m)
{
    return pthread_mutex_lock(m);
}

static inline unsigned vendor_nap(void)
{
    return sleep(0);
}

#endif
