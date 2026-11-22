#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sched.h>

#include "dino_portability.h"

// On Windows this <sched.h> is winpthreads', a library dino_thread_* doesn't need.
#if defined(DINO__WARNINGS) && !defined(DINO__SCHED_H) && (defined(_SCHED_H) || defined(WIN_PTHREADS_SCHED_H))
#define DINO__SCHED_H

__typeof__(sched_yield) sched_yield __attribute__((deprecated(
    "sched_yield() needs winpthreads on Windows; use dino_thread_yield() from <dino/core/thread.h>")));

#endif
