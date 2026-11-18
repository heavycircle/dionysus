#include <dino/core/event.h>

#include <errno.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <dino/core/time.h>

#include "event_internal.h"

#define BATCH 64

struct dino_event_loop
{
    int epfd;
};

dino_event_loop_t dino_event_loop_create(void)
{
    struct dino_event_loop *loop = malloc(sizeof(*loop));
    if (!loop)
        return DINO_INVALID_EVENT_LOOP;
    loop->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epfd < 0)
    {
        free(loop);
        return DINO_INVALID_EVENT_LOOP;
    }
    return (dino_event_loop_t)loop;
}

void dino_event_loop_destroy(dino_event_loop_t handle)
{
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    if (!loop)
        return;
    close(loop->epfd);
    free(loop);
}

// EPOLLRDHUP: a peer that only shut down its sending side still reports
// HANGUP, as AFD's DISCONNECT does on Windows.
static int ctl(
    dino_event_loop_t handle, int op, dino_socket_t sock, uint32_t events, void *userdata)
{
    if (!dino__event_ctl_args_valid(handle, sock, events))
        return -1;
    struct epoll_event ev = { 0 };
    ev.events             = EPOLLRDHUP;
    if (events & DINO_EVENT_READ)
        ev.events |= EPOLLIN;
    if (events & DINO_EVENT_WRITE)
        ev.events |= EPOLLOUT;
    if (events & DINO_EVENT_ONESHOT)
        ev.events |= EPOLLONESHOT;
    ev.data.ptr = userdata;
    return epoll_ctl(((struct dino_event_loop *)handle)->epfd, op, (int)sock, &ev) == 0 ? 0 : -1;
}

int dino_event_add_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata)
{
    return ctl(loop, EPOLL_CTL_ADD, sock, events, userdata);
}

int dino_event_mod_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata)
{
    return ctl(loop, EPOLL_CTL_MOD, sock, events, userdata);
}

int dino_event_del_socket(dino_event_loop_t loop, dino_socket_t sock)
{
    if (!dino__event_ctl_args_valid(loop, sock, 0))
        return -1;
    // Linux before 2.6.9 required a non-NULL event even for DEL.
    struct epoll_event ev = { 0 };
    return epoll_ctl(((struct dino_event_loop *)loop)->epfd, EPOLL_CTL_DEL, (int)sock, &ev) == 0
        ? 0
        : -1;
}

static uint32_t from_epoll(uint32_t ev)
{
    uint32_t events = 0;
    if (ev & EPOLLIN)
        events |= DINO_EVENT_READ;
    if (ev & EPOLLOUT)
        events |= DINO_EVENT_WRITE;
    if (ev & EPOLLERR)
        events |= DINO_EVENT_ERROR;
    if (ev & (EPOLLHUP | EPOLLRDHUP))
        events |= DINO_EVENT_HANGUP;
    return events;
}

int dino_event_wait(dino_event_loop_t handle, dino_event_t *out, int max, int timeout_ms)
{
    if (!dino__event_wait_args_valid(handle, out, max))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;

    struct epoll_event evs[BATCH];
    int                cap = max < BATCH ? max : BATCH;
    uint64_t           deadline
        = timeout_ms > 0 ? dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000 : 0;
    int n;
    while ((n = epoll_wait(loop->epfd, evs, cap, timeout_ms)) < 0)
    {
        if (errno != EINTR)
            return -1;
        if (timeout_ms > 0)
        {
            uint64_t now = dino_time_monotonic_ns();
            if (now >= deadline)
                return 0;
            timeout_ms = (int)((deadline - now + 999999) / 1000000);
        }
    }
    for (int i = 0; i < n; i++)
    {
        out[i].events   = from_epoll(evs[i].events);
        out[i].userdata = evs[i].data.ptr;
    }
    return n;
}
