#include <dino/core/event.h>

#include <stdlib.h>
#include <winternl.h>

#include <winsock2.h>

#include <windows.h>

#include <dino/core/time.h>

#include "event_internal.h"
#include "pipe_internal.h"
#include "process_internal.h"

// Readiness on Windows comes from the AFD driver underneath Winsock, as in
// wepoll and libuv: IOCTL_AFD_POLL on a handle to \Device\Afd completes on an
// I/O completion port when a socket becomes ready. Each armed socket has one
// such poll in flight. Level-triggered sockets are polled again at the start
// of the next wait, so a socket that is still ready is reported again then
// rather than in a busy loop.

#define IOCTL_AFD_POLL 0x00012024

#define AFD_POLL_RECEIVE      0x0001
#define AFD_POLL_SEND         0x0004
#define AFD_POLL_DISCONNECT   0x0008
#define AFD_POLL_ABORT        0x0010
#define AFD_POLL_LOCAL_CLOSE  0x0020
#define AFD_POLL_ACCEPT       0x0080
#define AFD_POLL_CONNECT_FAIL 0x0100

#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS)0x00000103L)
#endif
#ifndef STATUS_PIPE_BROKEN
#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014BL)
#endif
#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED ((NTSTATUS)0xC0000120L)
#endif

#define SIO_BASE_HANDLE_     0x48000022
#define SIO_BSP_HANDLE_POLL_ 0x4800001D

#define BATCH 64

// Completion key of the packet dino_event_wake posts; polls complete with key 0.
#define WAKE_KEY 1

typedef struct
{
    HANDLE   Handle;
    ULONG    Events;
    NTSTATUS Status;
} AFD_POLL_HANDLE_INFO;

typedef struct
{
    LARGE_INTEGER        Timeout;
    ULONG                NumberOfHandles;
    ULONG                Exclusive;
    AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO;

typedef NTSTATUS(NTAPI *nt_create_file_fn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
    PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef NTSTATUS(NTAPI *nt_device_io_control_file_fn)(
    HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);
typedef NTSTATUS(NTAPI *nt_cancel_io_file_ex_fn)(HANDLE, PIO_STATUS_BLOCK, PIO_STATUS_BLOCK);

enum kind
{
    KIND_SOCKET,
    KIND_PIPE,
    KIND_PROCESS
};

// A watched socket, pipe or process. A socket is polled through AFD; a pipe,
// attached to the port itself, through a zero-byte read that completes when
// data arrives (or fails once every write end is closed). A process handle
// can't be attached to a port, so a thread-pool wait posts its item there when
// the process ends.
struct item
{
    // First: a completion's OVERLAPPED pointer is the address of io. A pipe's
    // OVERLAPPED starts with the same status word as the socket's
    // IO_STATUS_BLOCK, so io.afd.iosb.Status reads either.
    union
    {
        struct
        {
            IO_STATUS_BLOCK iosb;
            AFD_POLL_INFO   info;
        } afd;
        OVERLAPPED ov;
    } io;
    enum kind     kind;
    uintptr_t     key;    // the dino_socket_t or dino_pipe_t
    HANDLE        handle; // the base socket, or the pipe's HANDLE
    uint32_t      events;
    void         *userdata;
    ULONG         polled;  // AFD mask of the poll in flight
    int           pending; // a poll or read is in flight
    int           deleted; // freed when its last completion arrives
    int           queued;  // on the rearm queue
    char          none;    // the zero-byte read's buffer
    struct item  *next_queued;
    HANDLE        wait;  // a process's RegisterWaitForSingleObject
    HANDLE        port;  // where that wait posts
    volatile LONG fired; // the wait's callback ran, so its packet is in the port
};

struct dino_event_loop
{
    struct dino__event_timers    timers; // first: see event_internal.h
    SRWLOCK                      lock;
    HANDLE                       port;
    uint64_t                     id; // for pipes: port HANDLE values get reused
    HANDLE                       afd;
    nt_device_io_control_file_fn ioctl;
    nt_cancel_io_file_ex_fn      cancel;
    // Open addressing on (key, kind), linear probing; slots are NULL when empty.
    struct item **slots;
    size_t        cap, count;
    size_t        pending;
    struct item  *queue;
    volatile LONG wake_posted; // a wake packet is in the port, not yet reported
    volatile LONG user_wake;   // dino_event_wake was called since the last report
};

static size_t slot_of(const struct dino_event_loop *loop, uintptr_t key, enum kind kind)
{
    uint64_t h = ((uint64_t)key ^ (uint64_t)kind) * 0x9E3779B97F4A7C15ull;
    return (size_t)(h >> 32) & (loop->cap - 1);
}

static struct item *find(const struct dino_event_loop *loop, uintptr_t key, enum kind kind)
{
    for (size_t i = slot_of(loop, key, kind);; i = (i + 1) & (loop->cap - 1))
    {
        struct item *s = loop->slots[i];
        if (!s || (s->key == key && s->kind == kind))
            return s;
    }
}

static int grow(struct dino_event_loop *loop)
{
    size_t        old_cap = loop->cap;
    struct item **old     = loop->slots;
    struct item **slots   = (struct item **)calloc(old_cap * 2, sizeof(*slots));
    if (!slots)
        return -1;
    loop->slots = slots;
    loop->cap   = old_cap * 2;
    for (size_t i = 0; i < old_cap; i++)
    {
        if (!old[i])
            continue;
        size_t j = slot_of(loop, old[i]->key, old[i]->kind);
        while (slots[j])
            j = (j + 1) & (loop->cap - 1);
        slots[j] = old[i];
    }
    free((void *)old);
    return 0;
}

static int insert(struct dino_event_loop *loop, struct item *s)
{
    if ((loop->count + 1) * 2 > loop->cap && grow(loop) != 0)
        return -1;
    size_t i = slot_of(loop, s->key, s->kind);
    while (loop->slots[i])
        i = (i + 1) & (loop->cap - 1);
    loop->slots[i] = s;
    loop->count++;
    return 0;
}

// Backward-shift deletion keeps probe chains intact without tombstones.
static void remove_slot(struct dino_event_loop *loop, struct item *s)
{
    size_t mask = loop->cap - 1;
    size_t i    = slot_of(loop, s->key, s->kind);
    while (loop->slots[i] != s)
        i = (i + 1) & mask;
    loop->slots[i] = NULL;
    loop->count--;
    for (size_t j = (i + 1) & mask; loop->slots[j]; j = (j + 1) & mask)
    {
        size_t home = slot_of(loop, loop->slots[j]->key, loop->slots[j]->kind);
        // Move j back into the hole at i unless its home lies in (i, j].
        if (((j - home) & mask) >= ((j - i) & mask))
        {
            loop->slots[i] = loop->slots[j];
            loop->slots[j] = NULL;
            i              = j;
        }
    }
}

static ULONG to_afd(uint32_t events)
{
    ULONG afd = AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE | AFD_POLL_CONNECT_FAIL;
    if (events & DINO_EVENT_READ)
        afd |= AFD_POLL_RECEIVE | AFD_POLL_ACCEPT;
    if (events & DINO_EVENT_WRITE)
        afd |= AFD_POLL_SEND;
    return afd;
}

static uint32_t from_afd(ULONG afd)
{
    uint32_t events = 0;
    if (afd & (AFD_POLL_RECEIVE | AFD_POLL_ACCEPT))
        events |= DINO_EVENT_READ;
    if (afd & AFD_POLL_SEND)
        events |= DINO_EVENT_WRITE;
    if (afd & AFD_POLL_CONNECT_FAIL)
        events |= DINO_EVENT_ERROR | DINO_EVENT_WRITE;
    if (afd & (AFD_POLL_DISCONNECT | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE))
        events |= DINO_EVENT_HANGUP;
    if (afd & AFD_POLL_ABORT)
        events |= DINO_EVENT_ERROR;
    return events;
}

static int submit_pipe(struct dino_event_loop *loop, struct item *s)
{
    s->io.ov = (OVERLAPPED) { 0 };
    if (!ReadFile(s->handle, &s->none, 0, NULL, &s->io.ov) && GetLastError() != ERROR_IO_PENDING)
    {
        // Failing at once posts nothing; post the end of the pipe ourselves so
        // it's reported like a read that failed later.
        if (GetLastError() != ERROR_BROKEN_PIPE)
            return -1;
        s->io.afd.iosb.Status = STATUS_PIPE_BROKEN;
        if (!PostQueuedCompletionStatus(loop->port, 0, 0, &s->io.ov))
            return -1;
    }
    s->pending = 1;
    loop->pending++;
    return 0;
}

static VOID CALLBACK process_ended(PVOID context, BOOLEAN timed_out)
{
    (void)timed_out;
    struct item *s = context;
    InterlockedExchange(&s->fired, 1);
    PostQueuedCompletionStatus(s->port, 0, 0, &s->io.ov);
}

// Once only: the exit is reported once, and the item is never rearmed.
static int submit_process(struct dino_event_loop *loop, struct item *s)
{
    s->port = loop->port;
    if (!RegisterWaitForSingleObject(
            &s->wait, s->handle, process_ended, s, INFINITE, WT_EXECUTEONLYONCE))
        return -1;
    s->pending = 1;
    loop->pending++;
    return 0;
}

static int submit(struct dino_event_loop *loop, struct item *s)
{
    if (s->kind == KIND_PIPE)
        return submit_pipe(loop, s);
    if (s->kind == KIND_PROCESS)
        return submit_process(loop, s);
    ULONG mask                       = to_afd(s->events);
    s->io.afd.info.Timeout.QuadPart  = INT64_MAX;
    s->io.afd.info.NumberOfHandles   = 1;
    s->io.afd.info.Exclusive         = FALSE;
    s->io.afd.info.Handles[0].Handle = s->handle;
    s->io.afd.info.Handles[0].Events = mask;
    s->io.afd.info.Handles[0].Status = 0;
    s->io.afd.iosb.Status            = STATUS_PENDING;
    NTSTATUS st
        = loop->ioctl(loop->afd, NULL, NULL, &s->io.afd.iosb, &s->io.afd.iosb, IOCTL_AFD_POLL,
            &s->io.afd.info, sizeof(s->io.afd.info), &s->io.afd.info, sizeof(s->io.afd.info));
    // Success right away still posts a completion: the AFD handle doesn't skip
    // the port on success.
    if (st != STATUS_PENDING && st != 0)
        return -1;
    s->polled  = mask;
    s->pending = 1;
    loop->pending++;
    return 0;
}

static void enqueue(struct dino_event_loop *loop, struct item *s)
{
    if (s->queued)
        return;
    s->queued      = 1;
    s->next_queued = loop->queue;
    loop->queue    = s;
}

static void unqueue(struct dino_event_loop *loop, struct item *s)
{
    if (!s->queued)
        return;
    for (struct item **p = &loop->queue; *p; p = &(*p)->next_queued)
    {
        if (*p == s)
        {
            *p = s->next_queued;
            break;
        }
    }
    s->queued = 0;
}

static void cancel(struct dino_event_loop *loop, struct item *s)
{
    if (s->kind == KIND_PIPE)
    {
        CancelIoEx(s->handle, &s->io.ov);
        return;
    }
    IO_STATUS_BLOCK io;
    loop->cancel(loop->afd, &s->io.afd.iosb, &io);
}

static SOCKET base_socket(dino_socket_t sock)
{
    // Layered providers wrap the real socket; AFD only knows the base one.
    SOCKET base = INVALID_SOCKET;
    DWORD  bytes;
    if (WSAIoctl((SOCKET)sock, SIO_BASE_HANDLE_, NULL, 0, &base, sizeof(base), &bytes, NULL, NULL)
        == 0)
        return base;
    if (WSAIoctl(
            (SOCKET)sock, SIO_BSP_HANDLE_POLL_, NULL, 0, &base, sizeof(base), &bytes, NULL, NULL)
        == 0)
        return base;
    return (SOCKET)sock;
}

static HANDLE open_afd(HANDLE port, nt_create_file_fn create)
{
    static WCHAR      path[] = L"\\Device\\Afd\\Dino";
    UNICODE_STRING    name   = { sizeof(path) - sizeof(WCHAR), sizeof(path), path };
    OBJECT_ATTRIBUTES attr;
    InitializeObjectAttributes(&attr, &name, 0, NULL, NULL);
    HANDLE          afd;
    IO_STATUS_BLOCK io;
    if (create(&afd, SYNCHRONIZE, &attr, &io, NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN, 0, NULL, 0)
        != 0)
        return NULL;
    if (!CreateIoCompletionPort(afd, port, 0, 0)
        || !SetFileCompletionNotificationModes(afd, FILE_SKIP_SET_EVENT_ON_HANDLE))
    {
        CloseHandle(afd);
        return NULL;
    }
    return afd;
}

static volatile LONG64 loop_counter;

dino_event_loop_t dino_event_loop_create(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll)
        return DINO_INVALID_EVENT_LOOP;
    nt_create_file_fn create
        = (nt_create_file_fn)(void (*)(void))GetProcAddress(ntdll, "NtCreateFile");
    struct dino_event_loop *loop = calloc(1, sizeof(*loop));
    if (!create || !loop)
    {
        free(loop);
        return DINO_INVALID_EVENT_LOOP;
    }
    loop->ioctl = (nt_device_io_control_file_fn)(void (*)(void))GetProcAddress(
        ntdll, "NtDeviceIoControlFile");
    loop->cancel
        = (nt_cancel_io_file_ex_fn)(void (*)(void))GetProcAddress(ntdll, "NtCancelIoFileEx");
    loop->id    = (uint64_t)InterlockedIncrement64(&loop_counter);
    loop->cap   = 16;
    loop->slots = (struct item **)calloc(loop->cap, sizeof(*loop->slots));
    InitializeSRWLock(&loop->lock);
    // Neither handle is inheritable: CreateIoCompletionPort and NtCreateFile
    // without OBJ_INHERIT never make one that is.
    loop->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (loop->port)
        loop->afd = open_afd(loop->port, create);
    int timers = loop->port ? dino__event_timers_init(&loop->timers) : -1;
    if (!loop->ioctl || !loop->cancel || !loop->slots || !loop->afd || timers != 0)
    {
        if (timers == 0)
            dino__event_timers_destroy(&loop->timers);
        if (loop->afd)
            CloseHandle(loop->afd);
        if (loop->port)
            CloseHandle(loop->port);
        free((void *)loop->slots);
        free(loop);
        return DINO_INVALID_EVENT_LOOP;
    }
    return (dino_event_loop_t)loop;
}

// A completion for s: returns its event, or 0 when there is nothing to report.
// Called with the lock held.
static uint32_t complete(struct dino_event_loop *loop, struct item *s)
{
    s->pending = 0;
    loop->pending--;
    if (s->deleted)
    {
        free(s);
        return 0;
    }
    if (s->kind == KIND_PROCESS)
        return DINO_EVENT_EXIT;
    // A cancelled poll's info still holds stale bits; mod_socket cancelled it to
    // poll for something else.
    NTSTATUS status = s->io.afd.iosb.Status;
    if (status == STATUS_CANCELLED
        || (s->kind == KIND_SOCKET && s->io.afd.info.NumberOfHandles < 1))
    {
        if (submit(loop, s) != 0)
            return DINO_EVENT_ERROR | DINO_EVENT_HANGUP;
        return 0;
    }
    uint32_t events = DINO_EVENT_ERROR;
    if (s->kind == KIND_PIPE && status == STATUS_PIPE_BROKEN)
        events = DINO_EVENT_READ | DINO_EVENT_HANGUP;
    else if (s->kind == KIND_PIPE && status == 0)
        events = DINO_EVENT_READ;
    else if (status == 0)
        events = from_afd(s->io.afd.info.Handles[0].Events);
    events &= (s->events & (DINO_EVENT_READ | DINO_EVENT_WRITE)) | DINO_EVENT_ERROR
        | DINO_EVENT_HANGUP;
    if (!events)
    {
        if (submit(loop, s) != 0)
            return DINO_EVENT_ERROR | DINO_EVENT_HANGUP;
        return 0;
    }
    if (!(s->events & DINO_EVENT_ONESHOT))
        enqueue(loop, s);
    return events;
}

// Lets go of an item already out of the table. Whatever is still in flight
// for it completes into the port, where complete() frees it.
static void release(struct dino_event_loop *loop, struct item *s)
{
    if (s->kind == KIND_PROCESS)
    {
        // Waits for a callback that is running; the callback never takes the
        // lock, so holding it here can't deadlock.
        UnregisterWaitEx(s->wait, INVALID_HANDLE_VALUE);
        if (s->pending && !s->fired)
        {
            s->pending = 0;
            loop->pending--;
        }
    }
    else if (s->pending)
    {
        cancel(loop, s);
    }
    if (s->pending)
        s->deleted = 1;
    else
        free(s);
}

void dino_event_loop_destroy(dino_event_loop_t handle)
{
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    if (!loop)
        return;
    AcquireSRWLockExclusive(&loop->lock);
    for (size_t i = 0; i < loop->cap; i++)
    {
        if (loop->slots[i])
            release(loop, loop->slots[i]);
    }
    // The kernel still writes into a cancelled poll's state until its
    // completion is posted, so wait for every one before freeing.
    while (loop->pending > 0)
    {
        OVERLAPPED_ENTRY e[BATCH];
        ULONG            n = 0;
        if (!GetQueuedCompletionStatusEx(loop->port, e, BATCH, &n, INFINITE, FALSE))
            break;
        for (ULONG i = 0; i < n; i++)
        {
            if (e[i].lpOverlapped)
                complete(loop, (struct item *)e[i].lpOverlapped);
        }
    }
    ReleaseSRWLockExclusive(&loop->lock);
    CloseHandle(loop->afd);
    CloseHandle(loop->port);
    dino__event_timers_destroy(&loop->timers);
    free((void *)loop->slots);
    free(loop);
}

// add/mod/del for both kinds; the arguments are already checked.
static int add_item(struct dino_event_loop *loop, enum kind kind, uintptr_t key, HANDLE handle,
    uint32_t events, void *userdata)
{
    struct item *s = calloc(1, sizeof(*s));
    if (!s)
        return -1;
    s->kind     = kind;
    s->key      = key;
    s->handle   = handle;
    s->events   = events;
    s->userdata = userdata;

    AcquireSRWLockExclusive(&loop->lock);
    int rc = -1;
    if (!find(loop, key, kind) && insert(loop, s) == 0)
    {
        rc = submit(loop, s);
        if (rc != 0)
            remove_slot(loop, s);
    }
    ReleaseSRWLockExclusive(&loop->lock);
    if (rc != 0)
        free(s);
    return rc;
}

static int mod_item(
    struct dino_event_loop *loop, enum kind kind, uintptr_t key, uint32_t events, void *userdata)
{
    AcquireSRWLockExclusive(&loop->lock);
    struct item *s  = find(loop, key, kind);
    int          rc = -1;
    if (s)
    {
        s->events   = events;
        s->userdata = userdata;
        rc          = 0;
        unqueue(loop, s);
        if (!s->pending)
            rc = submit(loop, s);
        else if (kind == KIND_SOCKET && s->polled != to_afd(events))
            cancel(loop, s); // its completion resubmits with the new mask
    }
    ReleaseSRWLockExclusive(&loop->lock);
    return rc;
}

static int del_item(struct dino_event_loop *loop, enum kind kind, uintptr_t key)
{
    AcquireSRWLockExclusive(&loop->lock);
    struct item *s = find(loop, key, kind);
    if (s)
    {
        remove_slot(loop, s);
        unqueue(loop, s);
        release(loop, s);
    }
    ReleaseSRWLockExclusive(&loop->lock);
    return s ? 0 : -1;
}

int dino_event_add_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata)
{
    if (!dino__event_ctl_args_valid(loop, sock, events))
        return -1;
    return add_item((struct dino_event_loop *)loop, KIND_SOCKET, sock, (HANDLE)base_socket(sock),
        events, userdata);
}

int dino_event_mod_socket(
    dino_event_loop_t loop, dino_socket_t sock, uint32_t events, void *userdata)
{
    if (!dino__event_ctl_args_valid(loop, sock, events))
        return -1;
    return mod_item((struct dino_event_loop *)loop, KIND_SOCKET, sock, events, userdata);
}

int dino_event_del_socket(dino_event_loop_t loop, dino_socket_t sock)
{
    if (!dino__event_ctl_args_valid(loop, sock, 0))
        return -1;
    return del_item((struct dino_event_loop *)loop, KIND_SOCKET, sock);
}

int dino_event_add_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata)
{
    if (!dino__event_pipe_args_valid(loop, events) || dino__pipe_is_read_end(pipe) != 1)
        return -1;
    struct dino_event_loop *l = (struct dino_event_loop *)loop;
    if (dino__pipe_attach(pipe, (intptr_t)l->port, l->id) != 0)
        return -1;
    return add_item(l, KIND_PIPE, pipe, (HANDLE)dino__pipe_to_os(pipe), events, userdata);
}

int dino_event_mod_pipe(dino_event_loop_t loop, dino_pipe_t pipe, uint32_t events, void *userdata)
{
    if (!dino__event_pipe_args_valid(loop, events) || pipe == DINO_INVALID_PIPE)
        return -1;
    return mod_item((struct dino_event_loop *)loop, KIND_PIPE, pipe, events, userdata);
}

int dino_event_del_pipe(dino_event_loop_t loop, dino_pipe_t pipe)
{
    if (loop == DINO_INVALID_EVENT_LOOP || pipe == DINO_INVALID_PIPE)
        return -1;
    return del_item((struct dino_event_loop *)loop, KIND_PIPE, pipe);
}

// Polls again every level-triggered item reported by the last wait. One that
// can't be polled any more is reported as an error, once.
static int flush_queue(struct dino_event_loop *loop, dino_event_t *out, int max)
{
    int n = 0;
    for (struct item **p = &loop->queue; *p && n < max;)
    {
        struct item *s = *p;
        *p             = s->next_queued;
        s->queued      = 0;
        if (s->pending)
            continue;
        if (submit(loop, s) != 0)
        {
            out[n].events   = DINO_EVENT_ERROR | DINO_EVENT_HANGUP;
            out[n].userdata = s->userdata;
            n++;
        }
    }
    return n;
}

int dino_event_wait(dino_event_loop_t handle, dino_event_t *out, int max, int timeout_ms)
{
    if (!dino__event_wait_args_valid(handle, out, max))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    uint64_t                deadline
        = dino_time_monotonic_ns() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0) * 1000000;

    AcquireSRWLockExclusive(&loop->lock);
    int n = flush_queue(loop, out, max);
    ReleaseSRWLockExclusive(&loop->lock);
    if (n > 0)
        return n;

    int timers_first = dino__event_timers_turn(&loop->timers);
    for (;;)
    {
        n = timers_first ? dino__event_timers_collect(&loop->timers, out, max) : 0;
        int              left = dino__event_remaining_ms(timeout_ms, deadline);
        int              cap  = max - n < BATCH ? max - n : BATCH;
        OVERLAPPED_ENTRY e[BATCH];
        ULONG            got = 0;
        if (cap > 0)
        {
            int wait = n > 0 ? 0 : dino__event_timers_timeout(&loop->timers, left);
            if (!GetQueuedCompletionStatusEx(
                    loop->port, e, (ULONG)cap, &got, wait < 0 ? INFINITE : (DWORD)wait, FALSE))
            {
                if (GetLastError() != WAIT_TIMEOUT)
                    return -1;
                got = 0;
            }
        }

        AcquireSRWLockExclusive(&loop->lock);
        for (ULONG i = 0; i < got; i++)
        {
            if (e[i].lpCompletionKey == WAKE_KEY)
            {
                // Cleared before user_wake is read, so a wake from now on posts again.
                InterlockedExchange(&loop->wake_posted, 0);
                if (InterlockedExchange(&loop->user_wake, 0))
                {
                    out[n].events   = DINO_EVENT_WAKE;
                    out[n].userdata = NULL;
                    n++;
                }
                continue;
            }
            struct item *s        = (struct item *)e[i].lpOverlapped;
            void        *userdata = s->userdata;
            uint32_t     events   = complete(loop, s);
            if (events)
            {
                out[n].events   = events;
                out[n].userdata = userdata;
                n++;
            }
        }
        ReleaseSRWLockExclusive(&loop->lock);
        n += dino__event_timers_collect(&loop->timers, out + n, max - n);
        // Only cancellations, stale completions or a timer interrupt came in:
        // keep waiting for whatever time is left.
        if (n > 0 || left == 0)
            return n;
    }
}

static int post_wake(struct dino_event_loop *loop)
{
    if (InterlockedExchange(&loop->wake_posted, 1) != 0)
        return 0;
    if (PostQueuedCompletionStatus(loop->port, 0, WAKE_KEY, NULL))
        return 0;
    InterlockedExchange(&loop->wake_posted, 0);
    return -1;
}

int dino_event_wake(dino_event_loop_t handle)
{
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    if (!loop)
        return -1;
    InterlockedExchange(&loop->user_wake, 1);
    return post_wake(loop);
}

void dino__event_interrupt(dino_event_loop_t handle)
{
    (void)post_wake((struct dino_event_loop *)handle);
}

int dino_event_add_process(dino_event_loop_t loop, dino_process_t process, void *userdata)
{
    if (loop == DINO_INVALID_EVENT_LOOP || process == DINO_INVALID_PROCESS)
        return -1;
    return add_item((struct dino_event_loop *)loop, KIND_PROCESS, process,
        (HANDLE)dino__process_to_os(process), 0, userdata);
}

int dino_event_del_process(dino_event_loop_t loop, dino_process_t process)
{
    if (loop == DINO_INVALID_EVENT_LOOP || process == DINO_INVALID_PROCESS)
        return -1;
    return del_item((struct dino_event_loop *)loop, KIND_PROCESS, process);
}
