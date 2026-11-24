#ifndef DINO_CORE_THREAD_INTERNAL_H
#define DINO_CORE_THREAD_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#include <dino/core/thread.h>

// Internal to the SDK: its own threads, on the Win32 API on Windows, so
// programs that don't use pthreads don't carry winpthreads. Users write
// pthreads (dino/core/thread.h).

typedef struct dino_thread *dino__thread_t;

#define DINO__INVALID_THREAD ((dino__thread_t)0)

/**
 * A thread's entry point: an ordinary function on every platform.
 *
 * @param  arg  The argument given to dino__thread_create.
 * @return A result for dino__thread_join.
 */
typedef int (*dino__thread_fn_t)(void *arg);

/**
 * Start a thread running fn(arg).
 *
 * Its stack is 8 MB on every platform. The program's main thread keeps its
 * platform's own size (8 MB on Linux, 16 MB on Windows). Every thread must be
 * either joined or detached exactly once.
 *
 * @param  thread  Receives the handle, or DINO__INVALID_THREAD on failure.
 * @param  fn      Function to run.
 * @param  arg     Argument passed to fn.
 * @return 0 on success, -1 on failure.
 */
int dino__thread_create(dino__thread_t *thread, dino__thread_fn_t fn, void *arg);

/**
 * Wait for a thread to finish and release it. The handle is invalid afterwards.
 *
 * @param  thread  Handle from dino__thread_create, not yet joined or detached.
 * @param  result  Receives fn's return value; may be NULL.
 * @return 0 on success, -1 for DINO__INVALID_THREAD or the calling thread itself.
 */
int dino__thread_join(dino__thread_t thread, int *result);

/**
 * Let a thread finish on its own; its resources are released when it does. The
 * handle is invalid afterwards.
 *
 * @param  thread  Handle from dino__thread_create, not yet joined or detached.
 * @return 0 on success, -1 for DINO__INVALID_THREAD.
 */
int dino__thread_detach(dino__thread_t thread);

/**
 * Give up the rest of the calling thread's time slice.
 */
void dino__thread_yield(void);

// Every thread's stack, the same on every platform, so code that fits on one
// fits on the other. Left to the platforms it was 8 MB on Linux and the
// executable's 16 MB on Windows, which also caps win32 at about 100 threads.
#define DINO__THREAD_STACK_BYTES ((size_t)8 * 1024 * 1024)

// The OS entry point has a different signature (and calling convention, on
// 32-bit Windows) per platform, so it runs this trampoline, which calls the
// user's plain int fn(void *). The block is shared by the thread and its
// handle: whichever lets go last, the finished thread or join/detach, frees it.
struct dino_thread
{
    dino__thread_fn_t fn;
    void             *arg;
    int               result;
    atomic_int        refs;
    uintptr_t         os;    // The thread's HANDLE (Windows) or pthread_t (POSIX).
    uint64_t          os_id; // Its thread ID, for spotting self-joins (Windows).
};

// Internal to the SDK (thread_posix.c, thread_win32.c). Inline, so each half
// compiles them in as it did when they were static in one file.
static inline void dino__thread_release(struct dino_thread *t)
{
    if (atomic_fetch_sub(&t->refs, 1) == 1)
        free(t);
}

// Runs the user's function, stores its result and drops the thread's reference.
static inline void dino__thread_run(struct dino_thread *t)
{
    t->result = t->fn(t->arg);
    dino__thread_release(t);
}

static inline struct dino_thread *dino__thread_alloc(dino__thread_fn_t fn, void *arg)
{
    struct dino_thread *t = calloc(1, sizeof(*t));
    if (t)
    {
        t->fn  = fn;
        t->arg = arg;
        atomic_init(&t->refs, 2);
    }
    return t;
}

#endif // DINO_CORE_THREAD_INTERNAL_H
