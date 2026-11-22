#include <dino/core/pipe.h>

#include <dino/core/time.h>

#include "error_internal.h"
#include "pipe_internal.h"

#define PIPE_BYTES 65536

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

// Handles are fd + 1, so fd 0 (possible when stdin was closed) isn't
// DINO_INVALID_PIPE. The value only travels in the pointer type; nothing is
// allocated and it is never dereferenced.
#define TO_HANDLE(fd) ((dino_pipe_t)(uintptr_t)((fd) + 1))
#define TO_FD(pipe)   ((int)((uintptr_t)(pipe) - 1))

int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end)
{
    *read_end  = DINO_INVALID_PIPE;
    *write_end = DINO_INVALID_PIPE;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return dino__fail_os();
    // Linux's default; set anyway so both platforms behave the same. Best
    // effort: an unprivileged process can't raise it past pipe-max-size.
    fcntl(fds[1], F_SETPIPE_SZ, PIPE_BYTES);
    *read_end  = TO_HANDLE(fds[0]);
    *write_end = TO_HANDLE(fds[1]);
    return 0;
}

int dino_pipe_read(dino_pipe_t pipe, void *buf, size_t len)
{
    if (pipe == DINO_INVALID_PIPE || (len > 0 && !buf))
        return dino__fail(DINO_EINVAL);

    ssize_t n;
    do
        n = read(TO_FD(pipe), buf, (size_t)dino__clamp_len(len));
    while (n < 0 && errno == EINTR);
    return n < 0 ? dino__fail_os() : (int)n;
}

int dino_pipe_write(dino_pipe_t pipe, const void *buf, size_t len)
{
    if (pipe == DINO_INVALID_PIPE || (len > 0 && !buf))
        return dino__fail(DINO_EINVAL);

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
        n = write(TO_FD(pipe), buf, (size_t)dino__clamp_len(len));
    while (n < 0 && errno == EINTR);
    // Now: the signal calls below have an errno of their own.
    int reason = n < 0 ? dino__error_from_os() : 0;

    if (n < 0 && errno == EPIPE && !already_pending)
    {
        const struct timespec now = { 0, 0 };
        while (sigtimedwait(&pipe_only, NULL, &now) < 0 && errno == EINTR)
            ;
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return n < 0 ? dino__fail(reason) : (int)n;
}

// After POLLOUT a pipe has room for at least PIPE_BUF bytes, so a write that
// size can't block.
int dino__pipe_write_timeout(
    dino_pipe_t pipe, const void *buf, size_t len, int timeout_ms, int *written)
{
    *written = 0;
    if (pipe == DINO_INVALID_PIPE)
        return dino__fail(DINO_EINVAL);

    struct pollfd pfd = { TO_FD(pipe), POLLOUT, 0 };
    uint64_t      deadline
        = dino_time_monotonic_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000;
    int rc;
    for (;;)
    {
        int wait = timeout_ms;
        if (timeout_ms > 0)
        {
            uint64_t now = dino_time_monotonic_ns();
            wait         = now >= deadline ? 0 : (int)((deadline - now + 999999) / 1000000);
        }
        rc = poll(&pfd, 1, wait);
        if (rc >= 0 || errno != EINTR)
            break;
    }
    if (rc == 0)
        return DINO_TIMEOUT;
    if (rc < 0)
        return dino__fail_os();
    if (pfd.revents & POLLNVAL)
        return dino__fail(DINO_EINVAL);
    // With no reader left (POLLERR) the write fails with EPIPE.
    int n = dino_pipe_write(pipe, buf, len < (size_t)PIPE_BUF ? len : (size_t)PIPE_BUF);
    if (n < 0)
        return -1;
    *written = n;
    return 0;
}

int dino_pipe_wait(dino_pipe_t pipe, int timeout_ms)
{
    if (pipe == DINO_INVALID_PIPE)
        return dino__fail(DINO_EINVAL);

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
            return pfd.revents & POLLNVAL ? dino__fail(DINO_EINVAL) : 0;
        if (rc == 0)
            return DINO_TIMEOUT;
        if (errno != EINTR)
            return dino__fail_os();
    }
}

int dino_pipe_close(dino_pipe_t pipe)
{
    if (pipe == DINO_INVALID_PIPE)
        return dino__fail(DINO_EINVAL);
    return close(TO_FD(pipe)) == 0 ? 0 : dino__fail_os();
}

intptr_t dino__pipe_to_os(dino_pipe_t pipe)
{
    return TO_FD(pipe);
}

int dino__pipe_create_for_child(dino_pipe_t *parent_end, dino_pipe_t *child_end, int parent_reads)
{
    dino_pipe_t r, w;
    if (dino_pipe_create(&r, &w) != 0)
        return -1;
    *parent_end = parent_reads ? r : w;
    *child_end  = parent_reads ? w : r;
    // In a parent with stdio closed the child's end can land on fd 0-2, where
    // the child's other stdio redirections would replace it before it is moved
    // into place. Keep it above them.
    int fd = TO_FD(*child_end);
    if (fd > 2)
        return 0;
    int moved = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (moved < 0)
        dino__error_set_os();
    close(fd);
    if (moved < 0)
    {
        dino_pipe_close(*parent_end);
        *parent_end = *child_end = DINO_INVALID_PIPE;
        return -1;
    }
    *child_end = TO_HANDLE(moved);
    return 0;
}

int dino__pipe_is_read_end(dino_pipe_t pipe)
{
    if (pipe == DINO_INVALID_PIPE)
        return dino__fail(DINO_EINVAL);
    int flags = fcntl(TO_FD(pipe), F_GETFL);
    if (flags < 0)
        return dino__fail_os();
    return (flags & O_ACCMODE) == O_RDONLY;
}

int dino__pipe_attach(dino_pipe_t pipe, intptr_t port, uint64_t loop_id)
{
    (void)port;
    (void)loop_id;
    return pipe == DINO_INVALID_PIPE ? dino__fail(DINO_EINVAL) : 0;
}
