#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <time.h>

#include "dino_portability.h"

// Both are declared with the POSIX clocks, which strict C modes leave out.
#if defined(DINO__WARNINGS) && !defined(DINO__TIME_H) && (defined(_TIME_H))
#define DINO__TIME_H

#ifdef CLOCK_REALTIME
__typeof__(nanosleep) nanosleep __attribute__((deprecated(
    "nanosleep() rounds down to whole milliseconds on Windows; use dino_sleep_us() from <dino/core/time.h>")));
__typeof__(clock_nanosleep) clock_nanosleep __attribute__((deprecated(
    "clock_nanosleep() rounds down to whole milliseconds and takes only CLOCK_REALTIME on Windows; use dino_sleep_us() from <dino/core/time.h>")));
#endif

#endif
