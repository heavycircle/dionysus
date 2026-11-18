#ifndef DINO_CORE_EVENT_INTERNAL_H
#define DINO_CORE_EVENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <dino/core/event.h>

#define DINO__EVENT_INTEREST (DINO_EVENT_READ | DINO_EVENT_WRITE | DINO_EVENT_ONESHOT)

static inline int dino__event_wait_args_valid(
    dino_event_loop_t loop, const dino_event_t *out, int max)
{
    return loop != DINO_INVALID_EVENT_LOOP && out != NULL && max > 0;
}

static inline int dino__event_ctl_args_valid(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events)
{
    return loop != DINO_INVALID_EVENT_LOOP && sock != DINO_INVALID_SOCKET
        && (events & ~DINO__EVENT_INTEREST) == 0;
}

#endif // DINO_CORE_EVENT_INTERNAL_H
