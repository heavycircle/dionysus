#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <poll.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__POLL_H) && (defined(_POLL_H) || defined(_SYS_POLL_H))
#define DINO__POLL_H

#ifdef _GNU_SOURCE
__typeof__(ppoll) ppoll __attribute__((deprecated(
    "ppoll() is Linux-only; use poll() from <poll.h>, which works on every target")));
#endif

#endif
