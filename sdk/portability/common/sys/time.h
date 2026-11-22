#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/time.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_TIME_H) && (defined(_SYS_TIME_H) || defined(_SYS_TIME_H_))
#define DINO__SYS_TIME_H

__typeof__(gettimeofday) gettimeofday __attribute__((deprecated(
    "gettimeofday() is obsolete in POSIX and a MinGW extra on Windows; use dino_time_unix_ms() or dino_time_monotonic_ns() from <dino/core/time.h>")));

#endif
