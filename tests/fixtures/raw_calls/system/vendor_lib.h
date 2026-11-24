// Stands in for a third-party header-only library included as a system header:
// its inline functions call flagged APIs, which the project can't change, so
// they must not warn.
#ifndef VENDOR_LIB_H
#define VENDOR_LIB_H

#pragma GCC system_header

#include <pthread.h>

static inline int vendor_set_clock(pthread_condattr_t *attr)
{
    return pthread_condattr_setclock(attr, 1);
}

// A macro's call is checked where it's used, in the project's own code.
#define VENDOR_CLOCK_MACRO(attr) pthread_condattr_setclock(attr, 1)

#endif
