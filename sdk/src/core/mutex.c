#include <dino/core/mutex.h>

#include <stdlib.h>

#ifdef _WIN32

#include <windows.h>

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

#else

#include <pthread.h>

dino_mutex_t dino_mutex_create(void)
{
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (!m)
        return DINO_INVALID_MUTEX;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);

    if (rc != 0)
    {
        free(m);
        return DINO_INVALID_MUTEX;
    }
    return (dino_mutex_t)m;
}

void dino_mutex_destroy(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return;
    pthread_mutex_t *m = (pthread_mutex_t *)mutex;
    pthread_mutex_destroy(m);
    free(m);
}

int dino_mutex_lock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    return pthread_mutex_lock((pthread_mutex_t *)mutex);
}

int dino_mutex_unlock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    return pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

int dino_mutex_trylock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return -1;
    return pthread_mutex_trylock((pthread_mutex_t *)mutex) == 0 ? 0 : -1;
}

#endif // _WIN32
