#ifndef DINO_CORE_MUTEX_H
#define DINO_CORE_MUTEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef uintptr_t dino_mutex_t;

#define DINO_INVALID_MUTEX ((dino_mutex_t)0)

/**
 * Allocate and initialise a recursive mutex.
 *
 * @return Opaque mutex handle, or DINO_INVALID_MUTEX on failure.
 */
dino_mutex_t dino_mutex_create(void);

/**
 * Destroy a mutex and free its memory.
 *
 * @param mutex  Handle returned by dino_mutex_create.
 */
void dino_mutex_destroy(dino_mutex_t mutex);

/**
 * Acquire the mutex, blocking if necessary.
 *
 * @param mutex  Handle to lock.
 * @return 0 on success, -1 on error.
 */
int dino_mutex_lock(dino_mutex_t mutex);

/**
 * Release the mutex once: a mutex locked n times is free after n unlocks.
 *
 * Only the thread holding the mutex can unlock it. From any other thread, or
 * once it's no longer locked, this returns -1 and leaves the mutex as it was.
 *
 * @param mutex  Handle to unlock.
 * @return 0 on success, -1 if the caller doesn't hold the mutex or on error.
 */
int dino_mutex_unlock(dino_mutex_t mutex);

/**
 * Attempt to acquire the mutex without blocking.
 *
 * @param mutex  Handle to try-lock.
 * @return 0 if acquired, DINO_TIMEOUT if another thread holds it (a lock that
 *         would have had to wait, like the other waits that run out of time),
 *         -1 if mutex is not a mutex.
 */
int dino_mutex_trylock(dino_mutex_t mutex);

/* ---- Condition variables ---- */

typedef uintptr_t dino_cond_t;

#define DINO_INVALID_COND ((dino_cond_t)0)

/** Returned by dino_cond_timedwait when the time ran out. */
#define DINO_TIMEOUT 1

/**
 * Allocate a condition variable.
 *
 * @return Opaque handle, or DINO_INVALID_COND on failure.
 */
dino_cond_t dino_cond_create(void);

/**
 * Destroy a condition variable. No thread may be waiting on it.
 *
 * @param cond  Handle returned by dino_cond_create.
 */
void dino_cond_destroy(dino_cond_t cond);

/**
 * Release mutex, wait until cond is signalled, and lock mutex again.
 *
 * mutex must be locked exactly once by the caller (it is recursive; waiting
 * while holding it more than once is undefined). Wake-ups can be spurious, so
 * wait in a loop that re-checks the condition:
 *
 *   dino_mutex_lock(m);
 *   while (!ready)
 *       dino_cond_wait(c, m);
 *   dino_mutex_unlock(m);
 *
 * @param cond   Condition to wait on.
 * @param mutex  Mutex protecting the condition.
 * @return 0 on success, -1 if the caller doesn't hold the mutex or on error.
 */
int dino_cond_wait(dino_cond_t cond, dino_mutex_t mutex);

/**
 * Like dino_cond_wait, but give up after timeout_ms; -1 waits forever, like
 * dino_cond_wait.
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
int dino_cond_timedwait(dino_cond_t cond, dino_mutex_t mutex, int timeout_ms);

/**
 * Wake one thread waiting on cond, if any.
 *
 * @param cond  Condition to signal.
 * @return 0 on success, -1 on error.
 */
int dino_cond_signal(dino_cond_t cond);

/**
 * Wake every thread waiting on cond.
 *
 * @param cond  Condition to broadcast.
 * @return 0 on success, -1 on error.
 */
int dino_cond_broadcast(dino_cond_t cond);

/* ---- Once ---- */

/** Storage for dino_once; initialise it with DINO_ONCE_INIT. */
typedef uintptr_t dino_once_t;

#define DINO_ONCE_INIT ((dino_once_t)0)

/**
 * Run fn exactly once per once flag, however many threads call this.
 *
 * Every call returns only after fn has finished, so whatever fn set up is ready
 * to use. fn must not call dino_once on the same flag.
 *
 * @param once  Flag, statically initialised to DINO_ONCE_INIT.
 * @param fn    Function to run.
 * @return 0 on success, -1 for a NULL argument or on error.
 */
int dino_once(dino_once_t *once, void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_MUTEX_H
