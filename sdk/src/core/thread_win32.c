#include <dino/core/thread.h>

#include <stdlib.h>

#include <windows.h>

#include "error_internal.h"
#include "thread_internal.h"

static DWORD WINAPI trampoline(LPVOID arg)
{
    dino__thread_run(arg);
    return 0;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg)
{
    *thread = DINO_INVALID_THREAD;
    if (!fn)
        return dino__fail(DINO_EINVAL);
    struct dino_thread *t = dino__thread_alloc(fn, arg);
    if (!t)
        return dino__fail(DINO_ENOMEM);

    DWORD  id;
    HANDLE handle = CreateThread(NULL, 0, trampoline, t, 0, &id);
    if (!handle)
    {
        dino__error_set_os();
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
        return dino__fail(DINO_EINVAL);

    if (WaitForSingleObject((HANDLE)t->os, INFINITE) != WAIT_OBJECT_0)
        return dino__fail_os();
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
        return dino__fail(DINO_EINVAL);

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
