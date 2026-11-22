#include <dino/core/mutex.h>

#include <stdlib.h>

#include <windows.h>

#include <dino/core/time.h>

#include "error_internal.h"

// LeaveCriticalSection by a thread that doesn't hold the section is undefined
// and can corrupt it, so the owner and recursion depth are kept alongside to
// fail such an unlock with -1, as pthreads' recursive mutexes do. Only the
// owner writes them; another thread reading owner can't see its own id there.
struct dino_mutex
{
    CRITICAL_SECTION cs;
    DWORD            owner;
    unsigned         depth;
};

static int held_by_caller(struct dino_mutex *m)
{
    return __atomic_load_n(&m->owner, __ATOMIC_RELAXED) == GetCurrentThreadId();
}

static void acquired(struct dino_mutex *m)
{
    __atomic_store_n(&m->owner, GetCurrentThreadId(), __ATOMIC_RELAXED);
    m->depth++;
}

dino_mutex_t dino_mutex_create(void)
{
    struct dino_mutex *m = malloc(sizeof(*m));
    if (!m)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_MUTEX;
    }
    InitializeCriticalSection(&m->cs);
    m->owner = 0;
    m->depth = 0;
    return (dino_mutex_t)m;
}

void dino_mutex_destroy(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return;
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    DeleteCriticalSection(&m->cs);
    free(m);
}

int dino_mutex_lock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    EnterCriticalSection(&m->cs);
    acquired(m);
    return 0;
}

int dino_mutex_unlock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (!held_by_caller(m))
        return dino__fail(DINO_EACCES);
    if (--m->depth == 0)
        __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    LeaveCriticalSection(&m->cs);
    return 0;
}

int dino_mutex_trylock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    // Held by another thread: not a failure, the same answer as a wait that
    // ran out of time.
    if (!TryEnterCriticalSection(&m->cs))
        return DINO_TIMEOUT;
    acquired(m);
    return 0;
}

dino_cond_t dino_cond_create(void)
{
    CONDITION_VARIABLE *cv = malloc(sizeof(CONDITION_VARIABLE));
    if (!cv)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_COND;
    }
    InitializeConditionVariable(cv);
    return (dino_cond_t)cv;
}

// Windows condition variables need no teardown.
void dino_cond_destroy(dino_cond_t cond)
{
    free((CONDITION_VARIABLE *)cond);
}

// SleepConditionVariableCS leaves and re-enters the section, so the owner is
// handed over around it. The caller holds the mutex exactly once.
static BOOL sleep_on(dino_cond_t cond, struct dino_mutex *m, DWORD wait)
{
    unsigned depth = m->depth;
    __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    m->depth    = 0;
    BOOL  woke  = SleepConditionVariableCS((CONDITION_VARIABLE *)cond, &m->cs, wait);
    DWORD error = GetLastError();
    __atomic_store_n(&m->owner, GetCurrentThreadId(), __ATOMIC_RELAXED);
    m->depth = depth;
    SetLastError(error);
    return woke;
}

int dino_cond_wait(dino_cond_t cond, dino_mutex_t mutex)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (!held_by_caller(m))
        return dino__fail(DINO_EACCES);
    return sleep_on(cond, m, INFINITE) ? 0 : dino__fail_os();
}

// The timeout counts scheduler ticks and can end up to a tick early, so keep
// waiting until the monotonic deadline has really passed.
int dino_cond_timedwait(dino_cond_t cond, dino_mutex_t mutex, int timeout_ms)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    if (timeout_ms < 0)
        return dino_cond_wait(cond, mutex);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (!held_by_caller(m))
        return dino__fail(DINO_EACCES);

    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        uint64_t now = dino_time_monotonic_ns();
        if (now >= deadline)
            return DINO_TIMEOUT;
        uint64_t left_ms = (deadline - now + 999999) / 1000000;
        // INFINITE is UINT32_MAX, so never pass it by accident.
        DWORD wait = left_ms >= INFINITE ? INFINITE - 1 : (DWORD)left_ms;
        if (sleep_on(cond, m, wait))
            return 0;
        if (GetLastError() != ERROR_TIMEOUT)
            return dino__fail_os();
    }
}

int dino_cond_signal(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return dino__fail(DINO_EINVAL);
    WakeConditionVariable((CONDITION_VARIABLE *)cond);
    return 0;
}

int dino_cond_broadcast(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return dino__fail(DINO_EINVAL);
    WakeAllConditionVariable((CONDITION_VARIABLE *)cond);
    return 0;
}

_Static_assert(sizeof(INIT_ONCE) <= sizeof(dino_once_t), "INIT_ONCE must fit in dino_once_t");

static BOOL CALLBACK run_once(PINIT_ONCE once, PVOID fn, PVOID *context)
{
    (void)once;
    (void)context;
    ((void (*)(void))fn)();
    return TRUE;
}

int dino_once(dino_once_t *once, void (*fn)(void))
{
    if (!once || !fn)
        return dino__fail(DINO_EINVAL);
    return InitOnceExecuteOnce((PINIT_ONCE)once, run_once, (PVOID)fn, NULL) ? 0 : dino__fail_os();
}
