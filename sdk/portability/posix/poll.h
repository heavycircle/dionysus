#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <poll.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__POLL_H) && (defined(_POLL_H) || defined(_SYS_POLL_H))
#define DINO__POLL_H

__typeof__(poll) poll __attribute__((deprecated(
    "poll() is POSIX-only (WSAPoll differs); use dino_poll() from <dino/core/io.h>")));

#endif
