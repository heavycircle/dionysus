#include <dino/core/event.h>

#include <stdlib.h>
#include <winternl.h>

#include <winsock2.h>

#include <windows.h>

#include <dino/core/time.h>

#include "event_internal.h"

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

struct sock_state
{
    IO_STATUS_BLOCK    iosb; // first: a completion's OVERLAPPED pointer is &iosb
    AFD_POLL_INFO      info;
    dino_socket_t      sock;
    SOCKET             base;
    uint32_t           events;
    void              *userdata;
    ULONG              polled;  // AFD mask of the poll in flight
    int                pending; // a poll is in flight
    int                deleted; // freed when its last completion arrives
    int                queued;  // on the rearm queue
    struct sock_state *next_queued;
};

struct dino_event_loop
{
    SRWLOCK                      lock;
    HANDLE                       port;
    HANDLE                       afd;
    nt_device_io_control_file_fn ioctl;
    nt_cancel_io_file_ex_fn      cancel;
    // Open addressing on sock, linear probing; slots are NULL when empty.
    struct sock_state **slots;
    size_t              cap, count;
    size_t              pending;
    struct sock_state  *queue;
    volatile LONG       wake_posted; // a wake packet is in the port, not yet reported
};

static size_t slot_of(const struct dino_event_loop *loop, dino_socket_t sock)
{
    uint64_t h = (uint64_t)sock * 0x9E3779B97F4A7C15ull;
    return (size_t)(h >> 32) & (loop->cap - 1);
}

static struct sock_state *find(const struct dino_event_loop *loop, dino_socket_t sock)
{
    for (size_t i = slot_of(loop, sock);; i = (i + 1) & (loop->cap - 1))
    {
        struct sock_state *s = loop->slots[i];
        if (!s || s->sock == sock)
            return s;
    }
}

static int grow(struct dino_event_loop *loop)
{
    size_t              old_cap = loop->cap;
    struct sock_state **old     = loop->slots;
    struct sock_state **slots   = (struct sock_state **)calloc(old_cap * 2, sizeof(*slots));
    if (!slots)
        return -1;
    loop->slots = slots;
    loop->cap   = old_cap * 2;
    for (size_t i = 0; i < old_cap; i++)
    {
        if (!old[i])
            continue;
        size_t j = slot_of(loop, old[i]->sock);
        while (slots[j])
            j = (j + 1) & (loop->cap - 1);
        slots[j] = old[i];
    }
    free((void *)old);
    return 0;
}

static int insert(struct dino_event_loop *loop, struct sock_state *s)
{
    if ((loop->count + 1) * 2 > loop->cap && grow(loop) != 0)
        return -1;
    size_t i = slot_of(loop, s->sock);
    while (loop->slots[i])
        i = (i + 1) & (loop->cap - 1);
    loop->slots[i] = s;
    loop->count++;
    return 0;
}

// Backward-shift deletion keeps probe chains intact without tombstones.
static void remove_slot(struct dino_event_loop *loop, dino_socket_t sock)
{
    size_t mask = loop->cap - 1;
    size_t i    = slot_of(loop, sock);
    while (loop->slots[i]->sock != sock)
        i = (i + 1) & mask;
    loop->slots[i] = NULL;
    loop->count--;
    for (size_t j = (i + 1) & mask; loop->slots[j]; j = (j + 1) & mask)
    {
        size_t home = slot_of(loop, loop->slots[j]->sock);
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

static int submit(struct dino_event_loop *loop, struct sock_state *s)
{
    ULONG mask                = to_afd(s->events);
    s->info.Timeout.QuadPart  = INT64_MAX;
    s->info.NumberOfHandles   = 1;
    s->info.Exclusive         = FALSE;
    s->info.Handles[0].Handle = (HANDLE)s->base;
    s->info.Handles[0].Events = mask;
    s->info.Handles[0].Status = 0;
    s->iosb.Status            = STATUS_PENDING;
    NTSTATUS st = loop->ioctl(loop->afd, NULL, NULL, &s->iosb, &s->iosb, IOCTL_AFD_POLL, &s->info,
        sizeof(s->info), &s->info, sizeof(s->info));
    // Success right away still posts a completion: the AFD handle doesn't skip
    // the port on success.
    if (st != STATUS_PENDING && st != 0)
        return -1;
    s->polled  = mask;
    s->pending = 1;
    loop->pending++;
    return 0;
}

static void enqueue(struct dino_event_loop *loop, struct sock_state *s)
{
    if (s->queued)
        return;
    s->queued      = 1;
    s->next_queued = loop->queue;
    loop->queue    = s;
}

static void unqueue(struct dino_event_loop *loop, struct sock_state *s)
{
    if (!s->queued)
        return;
    for (struct sock_state **p = &loop->queue; *p; p = &(*p)->next_queued)
    {
        if (*p == s)
        {
            *p = s->next_queued;
            break;
        }
    }
    s->queued = 0;
}

static void cancel(struct dino_event_loop *loop, struct sock_state *s)
{
    IO_STATUS_BLOCK io;
    loop->cancel(loop->afd, &s->iosb, &io);
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
    loop->cap   = 16;
    loop->slots = (struct sock_state **)calloc(loop->cap, sizeof(*loop->slots));
    InitializeSRWLock(&loop->lock);
    // Neither handle is inheritable: CreateIoCompletionPort and NtCreateFile
    // without OBJ_INHERIT never make one that is.
    loop->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (loop->port)
        loop->afd = open_afd(loop->port, create);
    if (!loop->ioctl || !loop->cancel || !loop->slots || !loop->afd)
    {
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
static uint32_t complete(struct dino_event_loop *loop, struct sock_state *s)
{
    s->pending = 0;
    loop->pending--;
    if (s->deleted)
    {
        free(s);
        return 0;
    }
    // A cancelled poll's info still holds stale bits; mod_socket cancelled it to
    // poll for something else.
    if (s->iosb.Status == STATUS_CANCELLED || s->info.NumberOfHandles < 1)
    {
        if (submit(loop, s) != 0)
            return DINO_EVENT_ERROR | DINO_EVENT_HANGUP;
        return 0;
    }
    uint32_t events = s->iosb.Status == 0 ? from_afd(s->info.Handles[0].Events) : DINO_EVENT_ERROR;
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

void dino_event_loop_destroy(dino_event_loop_t handle)
{
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    if (!loop)
        return;
    AcquireSRWLockExclusive(&loop->lock);
    for (size_t i = 0; i < loop->cap; i++)
    {
        struct sock_state *s = loop->slots[i];
        if (!s)
            continue;
        if (s->pending)
        {
            s->deleted = 1;
            cancel(loop, s);
        }
        else
        {
            free(s);
        }
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
                complete(loop, (struct sock_state *)e[i].lpOverlapped);
        }
    }
    ReleaseSRWLockExclusive(&loop->lock);
    CloseHandle(loop->afd);
    CloseHandle(loop->port);
    free((void *)loop->slots);
    free(loop);
}

int dino_event_add_socket(
    dino_event_loop_t handle, dino_socket_t sock, uint32_t events, void *userdata)
{
    if (!dino__event_ctl_args_valid(handle, sock, events))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    struct sock_state      *s    = calloc(1, sizeof(*s));
    if (!s)
        return -1;
    s->sock     = sock;
    s->base     = base_socket(sock);
    s->events   = events;
    s->userdata = userdata;

    AcquireSRWLockExclusive(&loop->lock);
    int rc = -1;
    if (!find(loop, sock) && insert(loop, s) == 0)
    {
        rc = submit(loop, s);
        if (rc != 0)
            remove_slot(loop, sock);
    }
    ReleaseSRWLockExclusive(&loop->lock);
    if (rc != 0)
        free(s);
    return rc;
}

int dino_event_mod_socket(
    dino_event_loop_t handle, dino_socket_t sock, uint32_t events, void *userdata)
{
    if (!dino__event_ctl_args_valid(handle, sock, events))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    AcquireSRWLockExclusive(&loop->lock);
    struct sock_state *s  = find(loop, sock);
    int                rc = -1;
    if (s)
    {
        s->events   = events;
        s->userdata = userdata;
        rc          = 0;
        unqueue(loop, s);
        if (!s->pending)
            rc = submit(loop, s);
        else if (s->polled != to_afd(events))
            cancel(loop, s); // its completion resubmits with the new mask
    }
    ReleaseSRWLockExclusive(&loop->lock);
    return rc;
}

int dino_event_del_socket(dino_event_loop_t handle, dino_socket_t sock)
{
    if (!dino__event_ctl_args_valid(handle, sock, 0))
        return -1;
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    AcquireSRWLockExclusive(&loop->lock);
    struct sock_state *s = find(loop, sock);
    if (s)
    {
        remove_slot(loop, sock);
        unqueue(loop, s);
        if (s->pending)
        {
            s->deleted = 1;
            cancel(loop, s);
        }
        else
        {
            free(s);
        }
    }
    ReleaseSRWLockExclusive(&loop->lock);
    return s ? 0 : -1;
}

// Polls again every level-triggered socket reported by the last wait. One that
// can't be polled any more is reported as an error, once.
static int flush_queue(struct dino_event_loop *loop, dino_event_t *out, int max)
{
    int n = 0;
    for (struct sock_state **p = &loop->queue; *p && n < max;)
    {
        struct sock_state *s = *p;
        *p                   = s->next_queued;
        s->queued            = 0;
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
        = timeout_ms > 0 ? dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000 : 0;

    AcquireSRWLockExclusive(&loop->lock);
    int n = flush_queue(loop, out, max);
    ReleaseSRWLockExclusive(&loop->lock);
    if (n > 0)
        return n;

    for (;;)
    {
        OVERLAPPED_ENTRY e[BATCH];
        ULONG            got = 0;
        DWORD            ms  = timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms;
        if (!GetQueuedCompletionStatusEx(
                loop->port, e, (ULONG)(max < BATCH ? max : BATCH), &got, ms, FALSE))
            return GetLastError() == WAIT_TIMEOUT ? 0 : -1;

        AcquireSRWLockExclusive(&loop->lock);
        for (ULONG i = 0; i < got; i++)
        {
            if (e[i].lpCompletionKey == WAKE_KEY)
            {
                // Cleared before reporting, so a wake from now on posts again.
                InterlockedExchange(&loop->wake_posted, 0);
                out[n].events   = DINO_EVENT_WAKE;
                out[n].userdata = NULL;
                n++;
                continue;
            }
            struct sock_state *s        = (struct sock_state *)e[i].lpOverlapped;
            void              *userdata = s->userdata;
            uint32_t           events   = complete(loop, s);
            if (events)
            {
                out[n].events   = events;
                out[n].userdata = userdata;
                n++;
            }
        }
        ReleaseSRWLockExclusive(&loop->lock);
        if (n > 0)
            return n;

        // Only cancellations and stale completions came in: keep waiting for
        // whatever time is left.
        if (timeout_ms > 0)
        {
            uint64_t now = dino_time_monotonic_ns();
            if (now >= deadline)
                return 0;
            timeout_ms = (int)((deadline - now + 999999) / 1000000);
        }
        else if (timeout_ms == 0)
        {
            return 0;
        }
    }
}

int dino_event_wake(dino_event_loop_t handle)
{
    struct dino_event_loop *loop = (struct dino_event_loop *)handle;
    if (!loop)
        return -1;
    if (InterlockedExchange(&loop->wake_posted, 1) != 0)
        return 0;
    if (PostQueuedCompletionStatus(loop->port, 0, WAKE_KEY, NULL))
        return 0;
    InterlockedExchange(&loop->wake_posted, 0);
    return -1;
}
