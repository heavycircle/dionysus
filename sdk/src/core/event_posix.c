#include <dino/core/event.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <dino/core/time.h>

#include "event_internal.h"
#include "pipe_internal.h"
#include "process_internal.h"

#define BATCH 64

struct dino_event_loop
{
    struct dino__event_timers timers; // first: see event_internal.h
    int                       epfd;
    int                       wakefd; // its epoll data.ptr is &wakefd, which no userdata can equal
    atomic_int                user_wake; // the wakefd was signalled by dino_event_wake, not a timer
    // Processes' pidfds sit in an epoll set of their own, nested in epfd (with
    // data.ptr &proc_epfd), so their data.ptr can point at a proc_item without
    // being mistaken for a socket's userdata. proc_lock covers the list and
    // every read of proc_epfd, so a deleted item is never reported.
    int               proc_epfd;
    dino_mutex_t      proc_lock;
    struct proc_item *procs;
};

struct proc_item
{
    dino_process_t    process;
    int               pidfd;
    void             *userdata;
    struct proc_item *next;
};

dino_event_loop_t dino_event_loop_create(void)
{
    struct dino_event_loop *loop = malloc(sizeof(*loop));
    if (!loop)
        return DINO_INVALID_EVENT_LOOP;
    if (dino__event_timers_init(&loop->timers) != 0)
    {
        free(loop);
        return DINO_INVALID_EVENT_LOOP;
    }
    atomic_init(&loop->user_wake, 0);
    loop->procs             = NULL;
    loop->proc_lock         = dino_mutex_create();
    loop->epfd              = epoll_create1(EPOLL_CLOEXEC);
    loop->wakefd            = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    loop->proc_epfd         = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event wake = { 0 }, procs = { 0 };
    wake.events    = EPOLLIN;
    wake.data.ptr  = &loop->wakefd;
    procs.events   = EPOLLIN;
    procs.data.ptr = &loop->proc_epfd;
    if (loop->epfd < 0 || loop->wakefd < 0 || loop->proc_epfd < 0
        || loop->proc_lock == DINO_INVALID_MUTEX
        || epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->wakefd, &wake) != 0
        || epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->proc_epfd, &procs) != 0)
    {
        if (loop->epfd >= 0)
            close(loop->epfd);
        if (loop->wakefd >= 0)
            close(loop->wakefd);
        if (loop->proc_epfd >= 0)
            close(loop->proc_epfd);
        dino_mutex_destroy(loop->proc_lock);
        dino__event_timers_destroy(&loop->timers);
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
    for (struct proc_item *it = loop->procs, *next; it; it = next)
    {
        next = it->next;
        close(it->pidfd);
        free(it);
    }
    close(loop->epfd);
    close(loop->wakefd);
    close(loop->proc_epfd);
    dino_mutex_destroy(loop->proc_lock);
    dino__event_timers_destroy(&loop->timers);
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

// A pipe is just another descriptor to epoll; all that differs is what may be
// asked for.
static int pipe_ctl(
    dino_event_loop_t loop, int op, dino_pipe_t pipe, uint32_t events, void *userdata)
{
    if (!dino__event_pipe_args_valid(loop, events) || dino__pipe_is_read_end(pipe) != 1)
        return -1;
    return ctl(loop, op, (dino_socket_t)dino__pipe_to_os(pipe), events, userdata);
}

int dino_event_add_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata)
{
    return pipe_ctl(loop, EPOLL_CTL_ADD, pipe, events, userdata);
}

int dino_event_mod_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata)
{
    return pipe_ctl(loop, EPOLL_CTL_MOD, pipe, events, userdata);
}

int dino_event_del_pipe(dino_event_loop_t loop, dino_pipe_t pipe)
{
    if (pipe == DINO_INVALID_PIPE)
        return -1;
    return dino_event_del_socket(loop, (dino_socket_t)dino__pipe_to_os(pipe));
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
    // A pipe whose writers are gone reports EPOLLHUP alone, but a read won't
    // block there (it returns 0): that's READ, as on Windows.
    if (ev & EPOLLHUP)
        events |= DINO_EVENT_READ | DINO_EVENT_HANGUP;
    if (ev & EPOLLRDHUP)
        events |= DINO_EVENT_HANGUP;
    return events;
}

// Each pidfd is EPOLLONESHOT: an exit is reported once, and the item stays
// until dino_event_del_process. Exits that don't fit in out stay ready.
static int collect_exits(struct dino_event_loop *loop, dino_event_t *out, int max)
{
    struct epoll_event evs[BATCH];
    dino_mutex_lock(loop->proc_lock);
    int got = epoll_wait(loop->proc_epfd, evs, max < BATCH ? max : BATCH, 0);
    for (int i = 0; i < got; i++)
    {
        out[i].events   = DINO_EVENT_EXIT;
        out[i].userdata = ((struct proc_item *)evs[i].data.ptr)->userdata;
    }
    dino_mutex_unlock(loop->proc_lock);
    return got < 0 ? 0 : got;
}

int dino_event_add_process(dino_event_loop_t handle, dino_process_t process, void *userdata)
{
    if (handle == DINO_INVALID_EVENT_LOOP || process == DINO_INVALID_PROCESS)
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    struct proc_item       *item = malloc(sizeof(*item));
    if (!item)
        return -1;
    item->process  = process;
    item->userdata = userdata;
    item->pidfd    = -1;

    int rc = -1;
    dino_mutex_lock(loop->proc_lock);
    struct proc_item *it = loop->procs;
    while (it && it->process != process)
        it = it->next;
    if (!it)
        item->pidfd = dino__pidfd_open(process);
    if (item->pidfd >= 0)
    {
        struct epoll_event ev = { 0 };
        ev.events             = EPOLLIN | EPOLLONESHOT;
        ev.data.ptr           = item;
        rc = epoll_ctl(loop->proc_epfd, EPOLL_CTL_ADD, item->pidfd, &ev) == 0 ? 0 : -1;
    }
    if (rc == 0)
    {
        item->next  = loop->procs;
        loop->procs = item;
    }
    dino_mutex_unlock(loop->proc_lock);
    if (rc != 0)
    {
        if (item->pidfd >= 0)
            close(item->pidfd);
        free(item);
    }
    return rc;
}

int dino_event_del_process(dino_event_loop_t handle, dino_process_t process)
{
    if (handle == DINO_INVALID_EVENT_LOOP || process == DINO_INVALID_PROCESS)
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    dino_mutex_lock(loop->proc_lock);
    struct proc_item **p = &loop->procs;
    while (*p && (*p)->process != process)
        p = &(*p)->next;
    struct proc_item *item = *p;
    if (item)
    {
        *p = item->next;
        // Closing the pidfd also drops it from proc_epfd.
        close(item->pidfd);
        free(item);
    }
    dino_mutex_unlock(loop->proc_lock);
    return item ? 0 : -1;
}

int dino_event_wait(dino_event_loop_t handle, dino_event_t *out, int max, int timeout_ms)
{
    if (!dino__event_wait_args_valid(handle, out, max))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;

    struct epoll_event evs[BATCH];
    int                timers_first = dino__event_timers_turn(&loop->timers);
    uint64_t           deadline
        = dino_time_monotonic_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000;
    for (;;)
    {
        int n    = timers_first ? dino__event_timers_collect(&loop->timers, out, max) : 0;
        int left = dino__event_remaining_ms(timeout_ms, deadline);
        int cap  = max - n < BATCH ? max - n : BATCH;
        int got  = 0;
        if (cap > 0)
        {
            int wait = n > 0 ? 0 : dino__event_timers_timeout(&loop->timers, left);
            got      = epoll_wait(loop->epfd, evs, cap, wait);
            if (got < 0 && errno != EINTR)
                return -1;
        }

        int procs_ready = 0;
        for (int i = 0; i < got; i++)
        {
            if (evs[i].data.ptr == &loop->proc_epfd)
            {
                procs_ready = 1;
                continue;
            }
            if (evs[i].data.ptr == &loop->wakefd)
            {
                // One read empties the counter: every wake so far becomes this one.
                uint64_t count;
                (void)!read(loop->wakefd, &count, sizeof(count));
                if (atomic_exchange(&loop->user_wake, 0))
                {
                    out[n].events   = DINO_EVENT_WAKE;
                    out[n].userdata = NULL;
                    n++;
                }
                continue;
            }
            out[n].events   = from_epoll(evs[i].events);
            out[n].userdata = evs[i].data.ptr;
            n++;
        }
        if (procs_ready && n < max)
            n += collect_exits(loop, out + n, max - n);
        n += dino__event_timers_collect(&loop->timers, out + n, max - n);
        // Nothing to report after a timer interrupt, EINTR or a wake meant for
        // someone else: wait again for what is left of the timeout.
        if (n > 0 || left == 0)
            return n;
    }
}

static int signal_wakefd(struct dino_event_loop *loop)
{
    uint64_t one = 1;
    ssize_t  rc  = write(loop->wakefd, &one, sizeof(one));
    // EAGAIN: the counter is full, so a wake is already pending.
    return rc == (ssize_t)sizeof(one) || errno == EAGAIN ? 0 : -1;
}

int dino_event_wake(dino_event_loop_t handle)
{
    if (handle == DINO_INVALID_EVENT_LOOP)
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    atomic_store(&loop->user_wake, 1);
    return signal_wakefd(loop);
}

void dino__event_interrupt(dino_event_loop_t handle)
{
    (void)signal_wakefd((struct dino_event_loop *)handle);
}
