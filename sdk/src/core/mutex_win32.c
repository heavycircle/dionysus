#include <dino/core/mutex.h>

#include <stdlib.h>

#include <windows.h>

#include <dino/core/time.h>

dino_mutex_t dino_mutex_create(void)
{
    CRITICAL_SECTION *cs = malloc(sizeof(CRITICAL_SECTION));
    if (!cs)
        return DINO_INVALID_MUTEX;
    InitializeCriticalSection(cs);
    return (dino_mutex_t)cs;
}

void dino_mutex_destroy(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return;
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)mutex;
    DeleteCriticalSection(cs);
    free(cs);
}

int dino_mutex_lock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    EnterCriticalSection((CRITICAL_SECTION *)mutex);
    return 0;
}

int dino_mutex_unlock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    LeaveCriticalSection((CRITICAL_SECTION *)mutex);
    return 0;
}

int dino_mutex_trylock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    return TryEnterCriticalSection((CRITICAL_SECTION *)mutex) ? 0 : -1;
}

dino_cond_t dino_cond_create(void)
{
    CONDITION_VARIABLE *cv = malloc(sizeof(CONDITION_VARIABLE));
    if (!cv)
        return DINO_INVALID_COND;
    InitializeConditionVariable(cv);
    return (dino_cond_t)cv;
}

// Windows condition variables need no teardown.
void dino_cond_destroy(dino_cond_t cond)
{
    free((CONDITION_VARIABLE *)cond);
}

int dino_cond_wait(dino_cond_t cond, dino_mutex_t mutex)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return -1;
    return SleepConditionVariableCS((CONDITION_VARIABLE *)cond, (CRITICAL_SECTION *)mutex, INFINITE)
        ? 0
        : -1;
}

// The timeout counts scheduler ticks and can end up to a tick early, so keep
// waiting until the monotonic deadline has really passed.
int dino_cond_timedwait(dino_cond_t cond, dino_mutex_t mutex, uint32_t timeout_ms)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return -1;

    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        uint64_t now = dino_time_monotonic_ns();
        if (now >= deadline)
            return DINO_TIMEOUT;
        uint64_t left_ms = (deadline - now + 999999) / 1000000;
        // INFINITE is UINT32_MAX, so never pass it by accident.
        DWORD wait = left_ms >= INFINITE ? INFINITE - 1 : (DWORD)left_ms;
        if (SleepConditionVariableCS((CONDITION_VARIABLE *)cond, (CRITICAL_SECTION *)mutex, wait))
            return 0;
        if (GetLastError() != ERROR_TIMEOUT)
            return -1;
    }
}

int dino_cond_signal(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return -1;
    WakeConditionVariable((CONDITION_VARIABLE *)cond);
    return 0;
}

int dino_cond_broadcast(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return -1;
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
        return -1;
    return InitOnceExecuteOnce((PINIT_ONCE)once, run_once, (PVOID)fn, NULL) ? 0 : -1;
}
