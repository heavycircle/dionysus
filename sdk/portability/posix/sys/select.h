#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/select.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_SELECT_H) && (defined(_SYS_SELECT_H))
#define DINO__SYS_SELECT_H

__typeof__(select) select __attribute__((deprecated(
    "select() takes SOCKET sets on Windows; use dino_select() from <dino/core/io.h>")));

// POSIX 2001, which glibc's strict C modes leave out: when unsure, no warning.
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) \
    || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(pselect) pselect __attribute__((deprecated(
    "pselect() is POSIX-only; use dino_select() from <dino/core/io.h>")));
#endif

#endif
