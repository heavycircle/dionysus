// pipe2.
#define _GNU_SOURCE

#include <dino/core/pipe.h>

#include <dino/core/time.h>

#include "pipe_internal.h"

#define PIPE_BYTES 65536

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

// Handles are fd + 1, so fd 0 (possible when stdin was closed) isn't
// DINO_INVALID_PIPE.
#define TO_HANDLE(fd) ((dino_pipe_t)(fd) + 1)
#define TO_FD(pipe)   ((int)((pipe) - 1))

int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end)
{
    *read_end  = DINO_INVALID_PIPE;
    *write_end = DINO_INVALID_PIPE;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return -1;
    // Linux's default; set anyway so both platforms behave the same. Best
    // effort: an unprivileged process can't raise it past pipe-max-size.
    fcntl(fds[1], F_SETPIPE_SZ, PIPE_BYTES);
    *read_end  = TO_HANDLE(fds[0]);
    *write_end = TO_HANDLE(fds[1]);
    return 0;
}

int dino_pipe_read(dino_pipe_t pipe, void *buf, int len)
{
    if (pipe == DINO_INVALID_PIPE || len < 0)
        return -1;

    ssize_t n;
    do
        n = read(TO_FD(pipe), buf, (size_t)len);
    while (n < 0 && errno == EINTR);
    return n < 0 ? -1 : (int)n;
}

int dino_pipe_write(dino_pipe_t pipe, const void *buf, int len)
{
    if (pipe == DINO_INVALID_PIPE || len < 0)
        return -1;

    // A write to a pipe with no reader raises SIGPIPE, which would end the
    // process. Pipes have no MSG_NOSIGNAL, so block it in this thread for the
    // write and consume the one it caused, leaving the mask and disposition as
    // they were. A SIGPIPE that was already pending is left for its owner.
    sigset_t pipe_only, old;
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_only, &old);
    sigset_t pending;
    sigpending(&pending);
    int already_pending = sigismember(&pending, SIGPIPE);

    ssize_t n;
    do
        n = write(TO_FD(pipe), buf, (size_t)len);
    while (n < 0 && errno == EINTR);

    if (n < 0 && errno == EPIPE && !already_pending)
    {
        const struct timespec now = { 0, 0 };
        while (sigtimedwait(&pipe_only, NULL, &now) < 0 && errno == EINTR)
            ;
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return n < 0 ? -1 : (int)n;
}

int dino_pipe_wait(dino_pipe_t pipe, int timeout_ms)
{
    if (pipe == DINO_INVALID_PIPE)
        return -1;

    struct pollfd pfd      = { TO_FD(pipe), POLLIN, 0 };
    uint64_t      deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        int wait = timeout_ms;
        if (timeout_ms > 0)
        {
            uint64_t now = dino_time_monotonic_ns();
            wait         = now >= deadline ? 0 : (int)((deadline - now + 999999) / 1000000);
        }
        int rc = poll(&pfd, 1, wait);
        if (rc > 0)
            return pfd.revents & POLLNVAL ? -1 : 0;
        if (rc == 0)
            return DINO_TIMEOUT;
        if (errno != EINTR)
            return -1;
    }
}

int dino_pipe_close(dino_pipe_t pipe)
{
    if (pipe == DINO_INVALID_PIPE)
        return -1;
    return close(TO_FD(pipe)) == 0 ? 0 : -1;
}

intptr_t dino__pipe_to_os(dino_pipe_t pipe)
{
    return TO_FD(pipe);
}
