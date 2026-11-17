#ifndef DINO_CORE_THREAD_INTERNAL_H
#define DINO_CORE_THREAD_INTERNAL_H

#include <stdatomic.h>
#include <stdint.h>

#include <dino/core/thread.h>

// The OS entry point has a different signature (and calling convention, on
// 32-bit Windows) per platform, so it runs this trampoline, which calls the
// user's plain int fn(void *). The block is shared by the thread and its
// handle: whichever lets go last, the finished thread or join/detach, frees it.
struct dino_thread
{
    dino_thread_fn fn;
    void          *arg;
    int            result;
    atomic_int     refs;
    uintptr_t      os;    // The thread's HANDLE (Windows) or pthread_t (POSIX).
    uint64_t       os_id; // Its thread ID, for spotting self-joins (Windows).
};

// Internal to the SDK (thread.c, thread_posix.c, thread_win32.c).
struct dino_thread *dino__thread_alloc(dino_thread_fn fn, void *arg);
void                dino__thread_release(struct dino_thread *t);
// Runs the user's function, stores its result and drops the thread's reference.
void dino__thread_run(struct dino_thread *t);

#endif // DINO_CORE_THREAD_INTERNAL_H
