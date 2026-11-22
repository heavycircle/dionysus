#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <signal.h>

#include "dino_portability.h"

// kill() is POSIX, so strict C modes leave it out: when unsure, no warning.
#if defined(DINO__WARNINGS) && !defined(DINO__SIGNAL_H) && (defined(_SIGNAL_H)) \
    && (defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) \
        || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE))
#define DINO__SIGNAL_H

__typeof__(kill) kill __attribute__((deprecated(
    "kill() is POSIX-only; to end a child use dino_process_kill() from <dino/core/process.h>")));

#endif
