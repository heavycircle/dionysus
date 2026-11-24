#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <pthread.h>

#include "dino_portability.h"

// pthreads behave the same on every target (winpthreads, linked statically on
// Windows), so only the call that doesn't is flagged: Windows refuses a
// monotonic clock for condition variables.
#if defined(DINO__WARNINGS) && !defined(DINO__PTHREAD_H) && (defined(_PTHREAD_H) || defined(WIN_PTHREADS_H))
#define DINO__PTHREAD_H

// glibc declares it only with POSIX feature macros, which strict C modes leave out.
#if !defined(__GLIBC__) || defined(__USE_XOPEN2K)
__typeof__(pthread_condattr_setclock) pthread_condattr_setclock __attribute__((deprecated(
    "pthread_condattr_setclock() fails with EINVAL for CLOCK_MONOTONIC on Windows; for a timed wait that ignores clock changes use dino_cond_init() and dino_cond_timedwait() from <dino/core/thread.h>")));
#endif

#endif
