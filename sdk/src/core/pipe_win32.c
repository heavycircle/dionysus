#include <dino/core/pipe.h>

#include <stdlib.h>
#include <wchar.h>

#include "error_internal.h"
#include "pipe_internal.h"

#define PIPE_BYTES 65536

#include <windows.h>

#include <dino/core/time.h>

// Anonymous pipes (CreatePipe) can't do overlapped I/O, so they can't be
// waited on or attached to an event loop's completion port. Each pipe is a
// uniquely named pipe instead: the server end comes from CreateNamedPipeW, the
// client end from CreateFileW.
struct dino_pipe
{
    HANDLE   handle;
    uint64_t loop_id; // the event loop whose port it is attached to, for good
    int      read_end;
    int      overlapped; // every end except a child's stdio
};

static volatile LONG pipe_counter;

static dino_pipe_t wrap(HANDLE h, int read_end, int overlapped)
{
    struct dino_pipe *p = calloc(1, sizeof(*p));
    if (!p)
    {
        CloseHandle(h);
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_PIPE;
    }
    p->handle     = h;
    p->read_end   = read_end;
    p->overlapped = overlapped;
    return (dino_pipe_t)p;
}

// Neither handle is inheritable: a process spawn duplicates the child's end
// into an inheritable handle itself.
static int create_pair(
    dino_pipe_t *server_end, dino_pipe_t *client_end, int server_reads, int client_overlapped)
{
    *server_end = *client_end = DINO_INVALID_PIPE;

    WCHAR name[64];
    _snwprintf(name, 64, L"\\\\.\\pipe\\dino-%lu-%ld", GetCurrentProcessId(),
        InterlockedIncrement(&pipe_counter));
    name[63] = 0;

    // FIRST_PIPE_INSTANCE and one instance: nobody else can create or hold a
    // second end under this name.
    DWORD open_mode = (server_reads ? PIPE_ACCESS_INBOUND : PIPE_ACCESS_OUTBOUND)
        | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE;
    DWORD  pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
    HANDLE server
        = CreateNamedPipeW(name, open_mode, pipe_mode, 1, PIPE_BYTES, PIPE_BYTES, 0, NULL);
    if (server == INVALID_HANDLE_VALUE)
        return dino__fail_os();
    HANDLE client = CreateFileW(name, server_reads ? GENERIC_WRITE : GENERIC_READ, 0, NULL,
        OPEN_EXISTING, client_overlapped ? FILE_FLAG_OVERLAPPED : 0, NULL);
    if (client == INVALID_HANDLE_VALUE)
    {
        dino__error_set_os();
        CloseHandle(server);
        return -1;
    }

    *server_end = wrap(server, server_reads, 1);
    *client_end = wrap(client, !server_reads, client_overlapped);
    if (*server_end == DINO_INVALID_PIPE || *client_end == DINO_INVALID_PIPE)
    {
        dino_pipe_close(*server_end);
        dino_pipe_close(*client_end);
        *server_end = *client_end = DINO_INVALID_PIPE;
        // Closing the half that was made reported nothing, or its own reason.
        return dino__fail(DINO_ENOMEM);
    }
    return 0;
}

int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end)
{
    if (!read_end || !write_end)
        return dino__fail(DINO_EINVAL);
    return create_pair(read_end, write_end, 1, 1);
}

int dino__pipe_create_for_child(dino_pipe_t *parent_end, dino_pipe_t *child_end, int parent_reads)
{
    return create_pair(parent_end, child_end, parent_reads, 0);
}

// Blocking I/O on an overlapped handle: wait on an event of our own. Its low
// bit keeps the completion off any port the pipe is attached to, where the
// event loop would take it for one of its own reads.
static BOOL transfer(struct dino_pipe *p, int write, void *buf, DWORD len, DWORD *n)
{
    *n = 0;
    if (!p->overlapped)
        return write ? WriteFile(p->handle, buf, len, n, NULL)
                     : ReadFile(p->handle, buf, len, n, NULL);

    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event)
        return FALSE;
    OVERLAPPED ov = { 0 };
    ov.hEvent     = (HANDLE)((ULONG_PTR)event | 1);
    BOOL ok       = write ? WriteFile(p->handle, buf, len, NULL, &ov)
                          : ReadFile(p->handle, buf, len, NULL, &ov);
    if (ok || GetLastError() == ERROR_IO_PENDING)
        ok = GetOverlappedResult(p->handle, &ov, n, TRUE);
    DWORD err = GetLastError();
    CloseHandle(event);
    SetLastError(err);
    return ok;
}

int dino_pipe_read(dino_pipe_t pipe, void *buf, size_t len)
{
    if (pipe == DINO_INVALID_PIPE || (len > 0 && !buf))
        return dino__fail(DINO_EINVAL);
    if (len == 0)
        return 0;

    DWORD n;
    if (!transfer((struct dino_pipe *)pipe, 0, buf, (DWORD)dino__clamp_len(len), &n))
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : dino__fail_os();
    return (int)n;
}

int dino_pipe_write(dino_pipe_t pipe, const void *buf, size_t len)
{
    if (pipe == DINO_INVALID_PIPE || (len > 0 && !buf))
        return dino__fail(DINO_EINVAL);
    if (len == 0)
        return 0;

    DWORD n;
    if (!transfer((struct dino_pipe *)pipe, 1, (void *)buf, (DWORD)dino__clamp_len(len), &n))
        return dino__fail_os();
    return (int)n;
}

// WaitForSingleObject with a timeout. The timeout counts scheduler ticks and
// can end up to a tick early, so keep waiting until the monotonic deadline has
// really passed.
static DWORD wait_for(HANDLE event, int timeout_ms)
{
    if (timeout_ms < 0)
        return WaitForSingleObject(event, INFINITE);
    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        uint64_t now  = dino_time_monotonic_ns();
        DWORD    wait = now >= deadline ? 0 : (DWORD)((deadline - now + 999999) / 1000000);
        DWORD    rc   = WaitForSingleObject(event, wait);
        if (rc != WAIT_TIMEOUT || dino_time_monotonic_ns() >= deadline)
            return rc;
    }
}

// A zero-byte read completes when data arrives, without taking any of it, or
// fails with ERROR_BROKEN_PIPE once every write end is closed.
int dino_pipe_wait(dino_pipe_t pipe, int timeout_ms)
{
    struct dino_pipe *p = (struct dino_pipe *)pipe;
    if (!p || !p->read_end || !p->overlapped)
        return dino__fail(DINO_EINVAL);

    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event)
        return dino__fail_os();
    OVERLAPPED ov = { 0 };
    ov.hEvent     = (HANDLE)((ULONG_PTR)event | 1);
    char  none;
    int   timed_out = 0;
    DWORD n;
    BOOL  ok = ReadFile(p->handle, &none, 0, NULL, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING)
    {
        if (wait_for(event, timeout_ms) == WAIT_TIMEOUT)
        {
            timed_out = 1;
            CancelIoEx(p->handle, &ov);
        }
        // Also waits out a cancelled read, which writes into ov until it's done.
        ok = GetOverlappedResult(p->handle, &ov, &n, TRUE);
    }
    DWORD err = GetLastError();
    CloseHandle(event);

    if (ok || err == ERROR_BROKEN_PIPE)
        return 0;
    if (timed_out && err == ERROR_OPERATION_ABORTED)
        return DINO_TIMEOUT;
    return dino__fail_code((long)err);
}

// As dino_pipe_wait does for reads: an overlapped write, cancelled when the
// time runs out. A cancelled write may still have sent part of buf.
int dino__pipe_write_timeout(
    dino_pipe_t pipe, const void *buf, size_t len, int timeout_ms, int *written)
{
    struct dino_pipe *p = (struct dino_pipe *)pipe;
    *written            = 0;
    if (!p || p->read_end)
        return dino__fail(DINO_EINVAL);
    if (len == 0)
        return 0;
    if (!p->overlapped)
    {
        int n = dino_pipe_write(pipe, buf, len);
        if (n < 0)
            return -1;
        *written = n;
        return 0;
    }

    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event)
        return dino__fail_os();
    OVERLAPPED ov   = { 0 };
    ov.hEvent       = (HANDLE)((ULONG_PTR)event | 1);
    int   timed_out = 0;
    DWORD n         = 0;
    BOOL  ok        = WriteFile(p->handle, buf, (DWORD)dino__clamp_len(len), NULL, &ov);
    if (ok || GetLastError() == ERROR_IO_PENDING)
    {
        if (!ok && wait_for(event, timeout_ms) == WAIT_TIMEOUT)
        {
            timed_out = 1;
            CancelIoEx(p->handle, &ov);
        }
        // Also waits out a cancelled write, which reads from buf until it's done.
        ok = GetOverlappedResult(p->handle, &ov, &n, TRUE);
    }
    DWORD err = GetLastError();
    CloseHandle(event);

    *written = (int)n;
    if (ok || n > 0)
        return 0;
    if (timed_out && err == ERROR_OPERATION_ABORTED)
        return DINO_TIMEOUT;
    return dino__fail_code((long)err);
}

int dino_pipe_close(dino_pipe_t pipe)
{
    struct dino_pipe *p = (struct dino_pipe *)pipe;
    if (!p)
        return dino__fail(DINO_EINVAL);
    if (!CloseHandle(p->handle))
    {
        dino__error_set_os();
        free(p);
        return -1;
    }
    free(p);
    return 0;
}

intptr_t dino__pipe_to_os(dino_pipe_t pipe)
{
    return pipe == DINO_INVALID_PIPE ? (intptr_t)INVALID_HANDLE_VALUE
                                     : (intptr_t)((struct dino_pipe *)pipe)->handle;
}

int dino__pipe_is_read_end(dino_pipe_t pipe)
{
    return pipe == DINO_INVALID_PIPE ? dino__fail(DINO_EINVAL)
                                     : ((struct dino_pipe *)pipe)->read_end;
}

int dino__pipe_attach(dino_pipe_t pipe, intptr_t port, uint64_t loop_id)
{
    struct dino_pipe *p = (struct dino_pipe *)pipe;
    if (!p || !p->overlapped || !loop_id)
        return dino__fail(DINO_EINVAL);
    // In another loop already, for good (see dino/core/event.h).
    if (p->loop_id)
        return p->loop_id == loop_id ? 0 : dino__fail(DINO_EEXIST);
    if (!CreateIoCompletionPort(p->handle, (HANDLE)port, 0, 0))
        return dino__fail_os();
    p->loop_id = loop_id;
    return 0;
}
