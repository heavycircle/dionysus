#ifndef DINO_CORE_MUTEX_INTERNAL_H
#define DINO_CORE_MUTEX_INTERNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct dino_mutex *dino__mutex_t;

#define DINO__INVALID_MUTEX ((dino__mutex_t)0)

/**
 * Allocate and initialise a recursive mutex.
 *
 * @return Opaque mutex handle, or DINO__INVALID_MUTEX on failure.
 */
dino__mutex_t dino__mutex_create(void);

/**
 * Destroy a mutex and free its memory.
 *
 * @param mutex  Handle returned by dino__mutex_create.
 */
void dino__mutex_destroy(dino__mutex_t mutex);

/**
 * Acquire the mutex, blocking if necessary.
 *
 * @param mutex  Handle to lock.
 * @return 0 on success, -1 on error.
 */
int dino__mutex_lock(dino__mutex_t mutex);

/**
 * Release the mutex once: a mutex locked n times is free after n unlocks.
 *
 * Only the thread holding the mutex can unlock it. From any other thread, or
 * once it's no longer locked, this returns -1 and leaves the mutex as it was.
 *
 * @param mutex  Handle to unlock.
 * @return 0 on success, -1 if the caller doesn't hold the mutex or on error.
 */
int dino__mutex_unlock(dino__mutex_t mutex);

/**
 * Attempt to acquire the mutex without blocking.
 *
 * @param mutex  Handle to try-lock.
 * @return 0 if acquired, DINO_TIMEOUT if another thread holds it (a lock that
 *         would have had to wait, like the other waits that run out of time),
 *         -1 if mutex is not a mutex.
 */
int dino__mutex_trylock(dino__mutex_t mutex);

/* ---- Condition variables ---- */

typedef struct dino_cond *dino__cond_t;

#define DINO__INVALID_COND ((dino__cond_t)0)

/** Returned by dino__cond_timedwait when the time ran out. */
#ifndef DINO_TIMEOUT
#define DINO_TIMEOUT 1
#endif

/**
 * Allocate a condition variable.
 *
 * @return Opaque handle, or DINO__INVALID_COND on failure.
 */
dino__cond_t dino__cond_create(void);

/**
 * Destroy a condition variable. No thread may be waiting on it.
 *
 * @param cond  Handle returned by dino__cond_create.
 */
void dino__cond_destroy(dino__cond_t cond);

/**
 * Release mutex, wait until cond is signalled, and lock mutex again.
 *
 * mutex must be locked exactly once by the caller: it is recursive, and a wait
 * while holding it more than once fails with -1 (DINO_EINVAL) instead of
 * sleeping with it still locked. Wake-ups can be spurious, so
 * wait in a loop that re-checks the condition:
 *
 *   dino__mutex_lock(m);
 *   while (!ready)
 *       dino__cond_wait(c, m);
 *   dino__mutex_unlock(m);
 *
 * @param cond   Condition to wait on.
 * @param mutex  Mutex protecting the condition.
 * @return 0 on success, -1 if the caller doesn't hold the mutex (DINO_EACCES),
 *         holds it more than once (DINO_EINVAL) or on error.
 */
int dino__cond_wait(dino__cond_t cond, dino__mutex_t mutex);

/**
 * Like dino__cond_wait, but give up after timeout_ms; -1 waits forever, like
 * dino__cond_wait.
 *
 * Measured on the monotonic clock (see dino/core/time.h), so changing the wall
 * clock doesn't shorten or extend it. DINO_TIMEOUT means at least timeout_ms
 * has passed.
 *
 * @param cond        Condition to wait on.
 * @param mutex       Mutex protecting the condition, locked once.
 * @param timeout_ms  Milliseconds to wait at most; -1 to wait forever.
 * @return 0 when woken, DINO_TIMEOUT when the time ran out, -1 if the caller
 *         doesn't hold the mutex or on error.
 */
int dino__cond_timedwait(dino__cond_t cond, dino__mutex_t mutex, int timeout_ms);

/**
 * Wake one thread waiting on cond, if any.
 *
 * @param cond  Condition to signal.
 * @return 0 on success, -1 on error.
 */
int dino__cond_signal(dino__cond_t cond);

/**
 * Wake every thread waiting on cond.
 *
 * @param cond  Condition to broadcast.
 * @return 0 on success, -1 on error.
 */
int dino__cond_broadcast(dino__cond_t cond);

/* ---- Once ---- */

/** Storage for dino_once; initialise it with DINO__ONCE_INIT. */
typedef uintptr_t dino__once_t;

#define DINO__ONCE_INIT ((dino__once_t)0)

/**
 * Run fn exactly once per once flag, however many threads call this.
 *
 * Every call returns only after fn has finished, so whatever fn set up is ready
 * to use. fn must not call dino_once on the same flag.
 *
 * @param once  Flag, statically initialised to DINO__ONCE_INIT.
 * @param fn    Function to run.
 * @return 0 on success, -1 for a NULL argument or on error.
 */
int dino__once(dino__once_t *once, void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_MUTEX_INTERNAL_H
