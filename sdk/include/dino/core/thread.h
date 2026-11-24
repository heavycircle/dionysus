#ifndef DINO_CORE_THREAD_H
#define DINO_CORE_THREAD_H

#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Threads, mutexes and condition variables are plain pthreads on every target
// (winpthreads on Windows, linked statically). This header adds what pthreads
// can't do the same way everywhere.

/** Returned by dino_cond_timedwait when the time ran out. */
#ifndef DINO_TIMEOUT
#define DINO_TIMEOUT 1
#endif

/**
 * Identify the calling thread.
 *
 * @return The OS thread ID (gettid / GetCurrentThreadId), unique among running
 *         threads of the system.
 */
uint64_t dino_thread_current_id(void);

/**
 * Initialize a condition variable for dino_cond_timedwait, in place of
 * pthread_cond_init. Its timed waits follow the monotonic clock, so setting the
 * system time doesn't shorten or extend them. pthread_condattr_setclock can't
 * do that on Windows. Destroy it with pthread_cond_destroy; signal, broadcast
 * and plain waits are the pthread calls.
 *
 * @param  cond  Condition variable to initialize.
 * @return 0 on success, -1 on failure.
 */
int dino_cond_init(pthread_cond_t *cond);

/**
 * Wait on a condition variable from dino_cond_init for at most timeout_ms.
 *
 * Like pthread_cond_wait, the caller must hold mutex, which is released while
 * waiting and held again on return, and a wakeup can be spurious: check the
 * condition in a loop. Each call waits up to timeout_ms anew.
 *
 * @param  cond        Condition variable from dino_cond_init.
 * @param  mutex       Mutex the caller holds.
 * @param  timeout_ms  -1 to wait forever, 0 to check once.
 * @return 0 when woken, DINO_TIMEOUT when the time ran out, -1 on error.
 */
int dino_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_THREAD_H
