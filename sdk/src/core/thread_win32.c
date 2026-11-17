#include <dino/core/thread.h>

#include <stdlib.h>

#include <windows.h>

#include "thread_internal.h"

static DWORD WINAPI trampoline(LPVOID arg)
{
    dino__thread_run(arg);
    return 0;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg)
{
    *thread               = DINO_INVALID_THREAD;
    struct dino_thread *t = fn ? dino__thread_alloc(fn, arg) : NULL;
    if (!t)
        return -1;

    DWORD  id;
    HANDLE handle = CreateThread(NULL, 0, trampoline, t, 0, &id);
    if (!handle)
    {
        free(t);
        return -1;
    }
    t->os    = (uintptr_t)handle;
    t->os_id = id;
    *thread  = (dino_thread_t)t;
    return 0;
}

int dino_thread_join(dino_thread_t thread, int *result)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || t->os_id == GetCurrentThreadId())
        return -1;

    if (WaitForSingleObject((HANDLE)t->os, INFINITE) != WAIT_OBJECT_0)
        return -1;
    CloseHandle((HANDLE)t->os);
    if (result)
        *result = t->result;
    dino__thread_release(t);
    return 0;
}

int dino_thread_detach(dino_thread_t thread)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t)
        return -1;

    CloseHandle((HANDLE)t->os);
    dino__thread_release(t);
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
