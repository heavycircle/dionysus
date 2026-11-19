#ifndef DINO_CORE_THREAD_H
#define DINO_CORE_THREAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef uintptr_t dino_thread_t;

#define DINO_INVALID_THREAD ((dino_thread_t)0)

/**
 * A thread's entry point: an ordinary function on every platform.
 *
 * @param  arg  The argument given to dino_thread_create.
 * @return A result for dino_thread_join.
 */
typedef int (*dino_thread_fn)(void *arg);

/**
 * Start a thread running fn(arg).
 *
 * Its stack is the platform default: 8 MB on the Linux targets (zig raises
 * musl's default) and 1 MB on Windows. Every thread must be either joined or
 * detached exactly once.
 *
 * @param  thread  Receives the handle, or DINO_INVALID_THREAD on failure.
 * @param  fn      Function to run.
 * @param  arg     Argument passed to fn.
 * @return 0 on success, -1 on failure.
 */
int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg);

/**
 * Wait for a thread to finish and release it. The handle is invalid afterwards.
 *
 * @param  thread  Handle from dino_thread_create, not yet joined or detached.
 * @param  result  Receives fn's return value; may be NULL.
 * @return 0 on success, -1 for DINO_INVALID_THREAD or the calling thread itself.
 */
int dino_thread_join(dino_thread_t thread, int *result);

/**
 * Let a thread finish on its own; its resources are released when it does. The
 * handle is invalid afterwards.
 *
 * @param  thread  Handle from dino_thread_create, not yet joined or detached.
 * @return 0 on success, -1 for DINO_INVALID_THREAD.
 */
int dino_thread_detach(dino_thread_t thread);

/**
 * Identify the calling thread.
 *
 * @return The OS thread ID (gettid / GetCurrentThreadId), unique among running
 *         threads of the system.
 */
uint64_t dino_thread_current_id(void);

/**
 * Give up the rest of the calling thread's time slice.
 */
void dino_thread_yield(void);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_THREAD_H
