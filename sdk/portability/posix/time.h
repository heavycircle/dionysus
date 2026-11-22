#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <time.h>

#include "dino_portability.h"

// Both are declared with the POSIX clocks, which strict C modes leave out.
#if defined(DINO__WARNINGS) && !defined(DINO__TIME_H) && (defined(_TIME_H))
#define DINO__TIME_H

#ifdef CLOCK_REALTIME
__typeof__(nanosleep) nanosleep __attribute__((deprecated(
    "nanosleep() is POSIX-only; use dino_sleep_us() from <dino/core/time.h>")));
__typeof__(clock_gettime) clock_gettime __attribute__((deprecated(
    "clock_gettime() is POSIX-only; use dino_time_monotonic_ns() from <dino/core/time.h>")));
#endif

#endif
