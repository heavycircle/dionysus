#include <dino/core/mutex.h>

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "error_internal.h"

// pthread functions return their error.
static int checked(int rc)
{
    return rc == 0 ? 0 : dino__fail_code(rc);
}

dino_mutex_t dino_mutex_create(void)
{
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (!m)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_MUTEX;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);

    if (rc != 0)
    {
        free(m);
        dino__error_set(dino__error_from_code(rc));
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
        return dino__fail(DINO_EINVAL);
    return checked(pthread_mutex_lock((pthread_mutex_t *)mutex));
}

int dino_mutex_unlock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_mutex_unlock((pthread_mutex_t *)mutex));
}

int dino_mutex_trylock(dino_mutex_t mutex)
{
    if (mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    // Held by another thread: not a failure, the same answer as a wait that
    // ran out of time.
    int rc = pthread_mutex_trylock((pthread_mutex_t *)mutex);
    return rc == EBUSY ? DINO_TIMEOUT : checked(rc);
}

dino_cond_t dino_cond_create(void)
{
    pthread_cond_t *c = malloc(sizeof(pthread_cond_t));
    if (!c)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO_INVALID_COND;
    }

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    int rc = pthread_cond_init(c, &attr);
    pthread_condattr_destroy(&attr);

    if (rc != 0)
    {
        free(c);
        dino__error_set(dino__error_from_code(rc));
        return DINO_INVALID_COND;
    }
    return (dino_cond_t)c;
}

void dino_cond_destroy(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return;
    pthread_cond_t *c = (pthread_cond_t *)cond;
    pthread_cond_destroy(c);
    free(c);
}

int dino_cond_wait(dino_cond_t cond, dino_mutex_t mutex)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex));
}

int dino_cond_timedwait(dino_cond_t cond, dino_mutex_t mutex, int timeout_ms)
{
    if (cond == DINO_INVALID_COND || mutex == DINO_INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    if (timeout_ms < 0)
        return dino_cond_wait(cond, mutex);

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    int rc = pthread_cond_timedwait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex, &deadline);
    if (rc == ETIMEDOUT)
        return DINO_TIMEOUT;
    return checked(rc);
}

int dino_cond_signal(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_cond_signal((pthread_cond_t *)cond));
}

int dino_cond_broadcast(dino_cond_t cond)
{
    if (cond == DINO_INVALID_COND)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_cond_broadcast((pthread_cond_t *)cond));
}

_Static_assert(
    sizeof(pthread_once_t) <= sizeof(dino_once_t), "pthread_once_t must fit in dino_once_t");
_Static_assert(PTHREAD_ONCE_INIT == 0, "DINO_ONCE_INIT is 0");

int dino_once(dino_once_t *once, void (*fn)(void))
{
    if (!once || !fn)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_once((pthread_once_t *)once, fn));
}
