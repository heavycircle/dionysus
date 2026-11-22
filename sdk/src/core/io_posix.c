#include <dino/core/io.h>

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>

#include <dino/core/time.h>

#include "error_internal.h"

// poll and select are never restarted after a signal handler, not even one
// installed with SA_RESTART. Retry with what is left of the timeout, as the
// pipe waits do, so a signal isn't mistaken for an error (Windows has none).
static int remaining(int timeout_ms, uint64_t deadline)
{
    if (timeout_ms < 0)
        return -1;
    uint64_t now = dino_time_monotonic_ns();
    return now >= deadline ? 0 : (int)((deadline - now + 999999) / 1000000);
}

// The DINO_* values happen to equal Linux's, but translating keeps that an
// implementation detail rather than something every target must share.
// POLLRDHUP: a peer that closed or shut down its side reports DINO_POLLHUP
// along with DINO_POLLIN, as on Windows, not POLLIN alone.
static short to_os_events(short events)
{
    short os = 0;
    if (events & DINO_POLLIN)
        os |= POLLIN | POLLRDHUP;
    if (events & DINO_POLLOUT)
        os |= POLLOUT;
    return os;
}

static short from_os_revents(short os)
{
    short revents = 0;
    if (os & POLLIN)
        revents |= DINO_POLLIN;
    if (os & POLLOUT)
        revents |= DINO_POLLOUT;
    if (os & (POLLERR | POLLNVAL))
        revents |= DINO_POLLERR;
    if (os & (POLLHUP | POLLRDHUP))
        revents |= DINO_POLLHUP;
    return revents;
}

int dino_poll(dino_pollfd_t *fds, int nfds, int timeout_ms)
{
    struct pollfd  stack_buf[64];
    struct pollfd *pfds = nfds <= 64 ? stack_buf : malloc((size_t)nfds * sizeof(struct pollfd));
    if (nfds < 0)
        return dino__fail(DINO_EINVAL);
    if (!pfds)
        return dino__fail(DINO_ENOMEM);

    for (int i = 0; i < nfds; i++)
    {
        pfds[i].fd      = (int)fds[i].sock;
        pfds[i].events  = to_os_events(fds[i].events);
        pfds[i].revents = 0;
    }

    uint64_t deadline
        = dino_time_monotonic_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000;
    int rc;
    while ((rc = poll(pfds, (nfds_t)nfds, remaining(timeout_ms, deadline))) < 0 && errno == EINTR)
        ;
    if (rc < 0)
        dino__error_set_os();

    for (int i = 0; i < nfds; i++)
    {
        fds[i].revents = 0;
        if (rc > 0)
            fds[i].revents = from_os_revents(pfds[i].revents);
    }

    if (pfds != stack_buf)
        free(pfds);

    return rc;
}

dino_fd_set_t dino_fd_set_create(void)
{
    fd_set *s = malloc(sizeof(fd_set));
    if (!s)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_FD_SET;
    }
    FD_ZERO(s);
    return (dino_fd_set_t)s;
}

void dino_fd_set_destroy(dino_fd_set_t set)
{
    if (set == DINO_INVALID_FD_SET)
        return;
    free((fd_set *)set);
}

void dino_fd_set_zero(dino_fd_set_t set)
{
    if (set == DINO_INVALID_FD_SET)
        return;
    FD_ZERO((fd_set *)set);
}

// FD_SET and friends index a fixed 1024-bit array without checking.
static int fits(dino_socket_t sock)
{
    return sock < FD_SETSIZE;
}

int dino_fd_set_add(dino_fd_set_t set, dino_socket_t sock)
{
    if (set == DINO_INVALID_FD_SET || !fits(sock))
        return dino__fail(DINO_EINVAL);
    FD_SET((int)sock, (fd_set *)set);
    return 0;
}

void dino_fd_set_del(dino_fd_set_t set, dino_socket_t sock)
{
    if (set == DINO_INVALID_FD_SET || !fits(sock))
        return;
    FD_CLR((int)sock, (fd_set *)set);
}

int dino_fd_set_has(dino_fd_set_t set, dino_socket_t sock)
{
    if (set == DINO_INVALID_FD_SET || !fits(sock))
        return 0;
    return FD_ISSET((int)sock, (fd_set *)set);
}

int dino_select(dino_socket_t max_fd, dino_fd_set_t rfds, dino_fd_set_t wfds, dino_fd_set_t efds,
    int timeout_ms)
{
    struct timeval  tv;
    struct timeval *tvp = NULL;
    if (timeout_ms >= 0)
    {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (suseconds_t)(timeout_ms % 1000) * 1000;
        tvp        = &tv;
    }

    // select leaves the sets undefined when a signal interrupts it, so each
    // retry starts from copies of the sets as given.
    fd_set *sets[3] = { (fd_set *)rfds, (fd_set *)wfds, (fd_set *)efds };
    fd_set  given[3];
    for (int i = 0; i < 3; i++)
        if (sets[i])
            memcpy(&given[i], sets[i], sizeof(fd_set));
    uint64_t deadline
        = dino_time_monotonic_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000;
    int rc;
    for (;;)
    {
        rc = select((int)max_fd, sets[0], sets[1], sets[2], tvp);
        if (rc >= 0 || errno != EINTR)
            break;
        for (int i = 0; i < 3; i++)
            if (sets[i])
                memcpy(sets[i], &given[i], sizeof(fd_set));
        if (tvp)
        {
            int left   = remaining(timeout_ms, deadline);
            tv.tv_sec  = left / 1000;
            tv.tv_usec = (suseconds_t)(left % 1000) * 1000;
        }
    }
    return rc < 0 ? dino__fail_os() : rc;
}
