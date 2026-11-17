#include <dino/core/pipe.h>

#include <dino/core/time.h>

#include "pipe_internal.h"

#define PIPE_BYTES 65536

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

intptr_t dino__pipe_to_os(dino_pipe_t pipe)
{
    return (intptr_t)pipe;
}
