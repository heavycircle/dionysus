// clang-format off: one redeclaration per call, laid out to be read.
#pragma GCC system_header
#include_next <poll.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__POLL_H)
#define DINO__POLL_H

__typeof__(poll) poll __attribute__((deprecated(
    "poll() is POSIX-only (WSAPoll differs); use dino_poll() from <dino/core/io.h>")));

#endif
