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

int dino__thread_create(dino__thread_t *thread, dino__thread_fn_t fn, void *arg)
{
    if (!thread)
        return dino__fail(DINO_EINVAL);
    *thread = DINO__INVALID_THREAD;
    if (!fn)
        return dino__fail(DINO_EINVAL);
    struct dino_thread *t = dino__thread_alloc(fn, arg);
    if (!t)
        return dino__fail(DINO_ENOMEM);

    DWORD id;
    // A reservation, as the executable's own stack size is: committed as used.
    HANDLE handle = CreateThread(
        NULL, DINO__THREAD_STACK_BYTES, trampoline, t, STACK_SIZE_PARAM_IS_A_RESERVATION, &id);
    if (!handle)
    {
        dino__error_set_os();
        free(t);
        return -1;
    }
    t->os    = (uintptr_t)handle;
    t->os_id = id;
    *thread  = (dino__thread_t)t;
    return 0;
}

int dino__thread_join(dino__thread_t thread, int *result)
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

int dino__thread_detach(dino__thread_t thread)
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

void dino__thread_yield(void)
{
    SwitchToThread();
}
