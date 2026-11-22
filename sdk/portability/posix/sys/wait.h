#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/wait.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_WAIT_H) && (defined(_SYS_WAIT_H))
#define DINO__SYS_WAIT_H

__typeof__(waitpid) waitpid __attribute__((deprecated(
    "waitpid() is POSIX-only; use dino_process_wait() from <dino/core/process.h>")));

__typeof__(wait) wait __attribute__((deprecated(
    "wait() is POSIX-only and takes any child; use dino_process_wait() from <dino/core/process.h>")));
// POSIX, left out by strict C modes; wait4 is BSD's: when unsure, no warning.
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) \
    || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(waitid) waitid __attribute__((deprecated(
    "waitid() is POSIX-only; use dino_process_wait() from <dino/core/process.h>")));
#endif
#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(wait4) wait4 __attribute__((deprecated(
    "wait4() is POSIX-only; use dino_process_wait() from <dino/core/process.h>")));
#endif

#endif
