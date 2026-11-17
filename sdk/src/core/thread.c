// gettid.
#define _GNU_SOURCE

#include <dino/core/thread.h>

#include <stdatomic.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

// The OS entry point has a different signature (and calling convention, on
// 32-bit Windows) per platform, so it runs this trampoline, which calls the
// user's plain int fn(void *). The block is shared by the thread and its
// handle: whichever lets go last, the finished thread or join/detach, frees it.
struct dino_thread
{
    dino_thread_fn fn;
    void          *arg;
    int            result;
    atomic_int     refs;
#ifdef _WIN32
    HANDLE handle;
    DWORD  id;
#else
    pthread_t pt;
#endif
};

static void release(struct dino_thread *t)
{
    if (atomic_fetch_sub(&t->refs, 1) == 1)
        free(t);
}

static void run(struct dino_thread *t)
{
    t->result = t->fn(t->arg);
    release(t);
}

static struct dino_thread *thread_alloc(dino_thread_fn fn, void *arg)
{
    struct dino_thread *t = calloc(1, sizeof(*t));
    if (t)
    {
        t->fn  = fn;
        t->arg = arg;
        atomic_init(&t->refs, 2);
    }
    return t;
}

#ifdef _WIN32

static DWORD WINAPI trampoline(LPVOID arg)
{
    run(arg);
    return 0;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg)
{
    *thread               = DINO_INVALID_THREAD;
    struct dino_thread *t = fn ? thread_alloc(fn, arg) : NULL;
    if (!t)
        return -1;

    t->handle = CreateThread(NULL, 0, trampoline, t, 0, &t->id);
    if (!t->handle)
    {
        free(t);
        return -1;
    }
    *thread = (dino_thread_t)t;
    return 0;
}

int dino_thread_join(dino_thread_t thread, int *result)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || t->id == GetCurrentThreadId())
        return -1;

    if (WaitForSingleObject(t->handle, INFINITE) != WAIT_OBJECT_0)
        return -1;
    CloseHandle(t->handle);
    if (result)
        *result = t->result;
    release(t);
    return 0;
}

int dino_thread_detach(dino_thread_t thread)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t)
        return -1;

    CloseHandle(t->handle);
    release(t);
    return 0;
}

uint64_t dino_thread_current_id(void)
{
    return GetCurrentThreadId();
}

void dino_thread_yield(void)
{
    SwitchToThread();
}

#else

static void *trampoline(void *arg)
{
    run(arg);
    return NULL;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg)
{
    *thread               = DINO_INVALID_THREAD;
    struct dino_thread *t = fn ? thread_alloc(fn, arg) : NULL;
    if (!t)
        return -1;

    if (pthread_create(&t->pt, NULL, trampoline, t) != 0)
    {
        free(t);
        return -1;
    }
    *thread = (dino_thread_t)t;
    return 0;
}

int dino_thread_join(dino_thread_t thread, int *result)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || pthread_equal(t->pt, pthread_self()))
        return -1;

    if (pthread_join(t->pt, NULL) != 0)
        return -1;
    if (result)
        *result = t->result;
    release(t);
    return 0;
}

int dino_thread_detach(dino_thread_t thread)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || pthread_detach(t->pt) != 0)
        return -1;

    release(t);
    return 0;
}

uint64_t dino_thread_current_id(void)
{
    return (uint64_t)gettid();
}

void dino_thread_yield(void)
{
    sched_yield();
}

#endif // _WIN32
