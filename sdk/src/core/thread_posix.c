// gettid.
#define _GNU_SOURCE

#include <dino/core/thread.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_internal.h"

_Static_assert(sizeof(pthread_t) <= sizeof(uintptr_t), "pthread_t must fit in dino_thread.os");

static void *trampoline(void *arg)
{
    dino__thread_run(arg);
    return NULL;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn fn, void *arg)
{
    *thread               = DINO_INVALID_THREAD;
    struct dino_thread *t = fn ? dino__thread_alloc(fn, arg) : NULL;
    if (!t)
        return -1;

    pthread_t pt;
    if (pthread_create(&pt, NULL, trampoline, t) != 0)
    {
        free(t);
        return -1;
    }
    t->os   = (uintptr_t)pt;
    *thread = (dino_thread_t)t;
    return 0;
}

int dino_thread_join(dino_thread_t thread, int *result)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || pthread_equal((pthread_t)t->os, pthread_self()))
        return -1;

    if (pthread_join((pthread_t)t->os, NULL) != 0)
        return -1;
    if (result)
        *result = t->result;
    dino__thread_release(t);
    return 0;
}

int dino_thread_detach(dino_thread_t thread)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || pthread_detach((pthread_t)t->os) != 0)
        return -1;

    dino__thread_release(t);
    return 0;
}

uint64_t dino_thread_current_id(void)
{
    return (uint64_t)gettid();
}

void dino_thread_yield(void)
{
    sched_yield();
}
