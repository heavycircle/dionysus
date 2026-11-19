#include <dino/core/io.h>

#include <poll.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

// The DINO_* values happen to equal Linux's, but translating keeps that an
// implementation detail rather than something every target must share.
static short to_os_events(short events)
{
    short os = 0;
    if (events & DINO_POLLIN)
        os |= POLLIN;
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
    if (os & POLLHUP)
        revents |= DINO_POLLHUP;
    return revents;
}

int dino_poll(dino_pollfd_t *fds, int nfds, int timeout_ms)
{
    struct pollfd  stack_buf[64];
    struct pollfd *pfds = nfds <= 64 ? stack_buf : malloc((size_t)nfds * sizeof(struct pollfd));
    if (!pfds)
        return -1;

    for (int i = 0; i < nfds; i++)
    {
        pfds[i].fd      = (int)fds[i].sock;
        pfds[i].events  = to_os_events(fds[i].events);
        pfds[i].revents = 0;
    }

    int rc = poll(pfds, (nfds_t)nfds, timeout_ms);

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
        return DINO_INVALID_FD_SET;
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
        return -1;
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

    return select((int)max_fd, rfds != DINO_INVALID_FD_SET ? (fd_set *)rfds : NULL,
        wfds != DINO_INVALID_FD_SET ? (fd_set *)wfds : NULL,
        efds != DINO_INVALID_FD_SET ? (fd_set *)efds : NULL, tvp);
}
