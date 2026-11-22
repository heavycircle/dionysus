#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/epoll.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_EPOLL_H) && (defined(_SYS_EPOLL_H))
#define DINO__SYS_EPOLL_H

__typeof__(epoll_create) epoll_create __attribute__((deprecated(
    "epoll_create() is Linux-only; use dino_event_loop_create() from <dino/core/event.h>")));
__typeof__(epoll_create1) epoll_create1 __attribute__((deprecated(
    "epoll_create1() is Linux-only; use dino_event_loop_create() from <dino/core/event.h>")));
__typeof__(epoll_ctl) epoll_ctl __attribute__((deprecated(
    "epoll_ctl() is Linux-only; use dino_event_add_socket() and friends from <dino/core/event.h>")));
__typeof__(epoll_wait) epoll_wait __attribute__((deprecated(
    "epoll_wait() is Linux-only; use dino_event_wait() from <dino/core/event.h>")));
__typeof__(epoll_pwait) epoll_pwait __attribute__((deprecated(
    "epoll_pwait() is Linux-only; use dino_event_wait() from <dino/core/event.h>")));

#endif
