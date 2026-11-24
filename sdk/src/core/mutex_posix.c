#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "error_internal.h"
#include "mutex_internal.h"

// pthread functions return their error.
static int checked(int rc)
{
    return rc == 0 ? 0 : dino__fail_code(rc);
}

// The owner and recursion depth are kept alongside, as on Windows, so that a
// wait while holding the mutex more than once fails instead of sleeping with
// it still locked. Only the owner writes them; another thread reading owner
// can't see its own id there.
struct dino_mutex
{
    pthread_mutex_t m;
    uintptr_t       owner; // pthread_self() of the holder, 0 when free
    unsigned        depth;
};

_Static_assert(sizeof(pthread_t) <= sizeof(uintptr_t), "pthread_t fits a uintptr_t");

static uintptr_t self_id(void)
{
    return (uintptr_t)pthread_self();
}

static int held_by_caller(struct dino_mutex *m)
{
    return __atomic_load_n(&m->owner, __ATOMIC_RELAXED) == self_id();
}

static void acquired(struct dino_mutex *m)
{
    __atomic_store_n(&m->owner, self_id(), __ATOMIC_RELAXED);
    m->depth++;
}

dino__mutex_t dino__mutex_create(void)
{
    struct dino_mutex *m = malloc(sizeof(*m));
    if (!m)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO__INVALID_MUTEX;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(&m->m, &attr);
    pthread_mutexattr_destroy(&attr);

    if (rc != 0)
    {
        free(m);
        dino__error_set(dino__error_from_code(rc));
        return DINO__INVALID_MUTEX;
    }
    m->owner = 0;
    m->depth = 0;
    return (dino__mutex_t)m;
}

void dino__mutex_destroy(dino__mutex_t mutex)
{
    if (mutex == DINO__INVALID_MUTEX)
        return;
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    pthread_mutex_destroy(&m->m);
    free(m);
}

int dino__mutex_lock(dino__mutex_t mutex)
{
    if (mutex == DINO__INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m  = (struct dino_mutex *)mutex;
    int                rc = pthread_mutex_lock(&m->m);
    if (rc != 0)
        return dino__fail_code(rc);
    acquired(m);
    return 0;
}

int dino__mutex_unlock(dino__mutex_t mutex)
{
    if (mutex == DINO__INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (!held_by_caller(m))
        return dino__fail(DINO_EACCES);
    if (--m->depth == 0)
        __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    return checked(pthread_mutex_unlock(&m->m));
}

int dino__mutex_trylock(dino__mutex_t mutex)
{
    if (mutex == DINO__INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    // Held by another thread: not a failure, the same answer as a wait that
    // ran out of time.
    int rc = pthread_mutex_trylock(&m->m);
    if (rc == EBUSY)
        return DINO_TIMEOUT;
    if (rc != 0)
        return dino__fail_code(rc);
    acquired(m);
    return 0;
}

// The caller must hold the mutex exactly once: a wait releases one level, so
// at a deeper level it would sleep with the mutex still locked and nobody
// could signal it.
static int check_waitable(struct dino_mutex *m)
{
    if (!held_by_caller(m))
        return dino__fail(DINO_EACCES);
    if (m->depth != 1)
        return dino__fail(DINO_EINVAL);
    return 0;
}

// pthread_cond_*wait release and retake the mutex, so the owner is handed over
// around them.
static int sleep_on(pthread_cond_t *c, struct dino_mutex *m, const struct timespec *deadline)
{
    __atomic_store_n(&m->owner, 0, __ATOMIC_RELAXED);
    m->depth = 0;
    int rc   = deadline ? pthread_cond_timedwait(c, &m->m, deadline) : pthread_cond_wait(c, &m->m);
    __atomic_store_n(&m->owner, self_id(), __ATOMIC_RELAXED);
    m->depth = 1;
    return rc;
}

dino__cond_t dino__cond_create(void)
{
    pthread_cond_t *c = malloc(sizeof(pthread_cond_t));
    if (!c)
    {
        dino__error_set(DINO_ENOMEM);
        return DINO__INVALID_COND;
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
        return DINO__INVALID_COND;
    }
    return (dino__cond_t)c;
}

void dino__cond_destroy(dino__cond_t cond)
{
    if (cond == DINO__INVALID_COND)
        return;
    pthread_cond_t *c = (pthread_cond_t *)cond;
    pthread_cond_destroy(c);
    free(c);
}

int dino__cond_wait(dino__cond_t cond, dino__mutex_t mutex)
{
    if (cond == DINO__INVALID_COND || mutex == DINO__INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (check_waitable(m) != 0)
        return -1;
    return checked(sleep_on((pthread_cond_t *)cond, m, NULL));
}

int dino__cond_timedwait(dino__cond_t cond, dino__mutex_t mutex, int timeout_ms)
{
    if (cond == DINO__INVALID_COND || mutex == DINO__INVALID_MUTEX)
        return dino__fail(DINO_EINVAL);
    if (timeout_ms < 0)
        return dino__cond_wait(cond, mutex);
    struct dino_mutex *m = (struct dino_mutex *)mutex;
    if (check_waitable(m) != 0)
        return -1;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    int rc = sleep_on((pthread_cond_t *)cond, m, &deadline);
    if (rc == ETIMEDOUT)
        return DINO_TIMEOUT;
    return checked(rc);
}

int dino__cond_signal(dino__cond_t cond)
{
    if (cond == DINO__INVALID_COND)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_cond_signal((pthread_cond_t *)cond));
}

int dino__cond_broadcast(dino__cond_t cond)
{
    if (cond == DINO__INVALID_COND)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_cond_broadcast((pthread_cond_t *)cond));
}

_Static_assert(
    sizeof(pthread_once_t) <= sizeof(dino__once_t), "pthread_once_t must fit in dino__once_t");
_Static_assert(PTHREAD_ONCE_INIT == 0, "DINO__ONCE_INIT is 0");

int dino__once(dino__once_t *once, void (*fn)(void))
{
    if (!once || !fn)
        return dino__fail(DINO_EINVAL);
    return checked(pthread_once((pthread_once_t *)once, fn));
}
