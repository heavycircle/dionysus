// pipe2.
#define _GNU_SOURCE

#include <dino/core/pipe.h>

#include <dino/core/time.h>

#define PIPE_BYTES 65536

#ifdef _WIN32

#include <windows.h>

int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end)
{
    *read_end  = DINO_INVALID_PIPE;
    *write_end = DINO_INVALID_PIPE;

    // Not inheritable: a process spawn passes the ends it needs explicitly.
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    HANDLE              r, w;
    if (!CreatePipe(&r, &w, &sa, PIPE_BYTES))
        return -1;
    *read_end  = (dino_pipe_t)r;
    *write_end = (dino_pipe_t)w;
    return 0;
}

int dino_pipe_read(dino_pipe_t pipe, void *buf, int len)
{
    if (pipe == DINO_INVALID_PIPE || len < 0)
        return -1;
    if (len == 0)
        return 0;

    DWORD n;
    if (!ReadFile((HANDLE)pipe, buf, (DWORD)len, &n, NULL))
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    return (int)n;
}

int dino_pipe_write(dino_pipe_t pipe, const void *buf, int len)
{
    if (pipe == DINO_INVALID_PIPE || len < 0)
        return -1;
    if (len == 0)
        return 0;

    DWORD n;
    if (!WriteFile((HANDLE)pipe, buf, (DWORD)len, &n, NULL))
        return -1;
    return (int)n;
}

// Anonymous pipes can't be waited on (no overlapped I/O), so peek until data
// or end of file shows up.
int dino_pipe_wait(dino_pipe_t pipe, int timeout_ms)
{
    if (pipe == DINO_INVALID_PIPE)
        return -1;

    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        DWORD available;
        if (!PeekNamedPipe((HANDLE)pipe, NULL, 0, NULL, &available, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        if (available > 0)
            return 0;
        if (timeout_ms >= 0 && dino_time_monotonic_ns() >= deadline)
            return DINO_TIMEOUT;
        dino_sleep_ms(1);
    }
}

int dino_pipe_close(dino_pipe_t pipe)
{
    if (pipe == DINO_INVALID_PIPE)
        return -1;
    return CloseHandle((HANDLE)pipe) ? 0 : -1;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

    ssize_t n;
    do
        n = write(TO_FD(pipe), buf, (size_t)len);
    while (n < 0 && errno == EINTR);
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

#endif // _WIN32
