#include <dino/core/thread.h>

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

#include "error_internal.h"
#include "thread_internal.h"

_Static_assert(sizeof(pthread_t) <= sizeof(uintptr_t), "pthread_t must fit in dino_thread.os");

static void *trampoline(void *arg)
{
    dino__thread_run(arg);
    return NULL;
}

int dino_thread_create(dino_thread_t *thread, dino_thread_fn_t fn, void *arg)
{
    if (!thread)
        return dino__fail(DINO_EINVAL);
    *thread = DINO_INVALID_THREAD;
    if (!fn)
        return dino__fail(DINO_EINVAL);
    struct dino_thread *t = dino__thread_alloc(fn, arg);
    if (!t)
        return dino__fail(DINO_ENOMEM);

    pthread_attr_t attr;
    int            rc = pthread_attr_init(&attr);
    if (rc == 0)
    {
        rc = pthread_attr_setstacksize(&attr, DINO__THREAD_STACK_BYTES);
        pthread_t pt;
        if (rc == 0)
            rc = pthread_create(&pt, &attr, trampoline, t);
        pthread_attr_destroy(&attr);
        if (rc == 0)
            t->os = (uintptr_t)pt;
    }
    if (rc != 0)
    {
        free(t);
        return dino__fail_code(rc);
    }
    *thread = (dino_thread_t)t;
    return 0;
}

int dino_thread_join(dino_thread_t thread, int *result)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t || pthread_equal((pthread_t)t->os, pthread_self()))
        return dino__fail(DINO_EINVAL);

    int rc = pthread_join((pthread_t)t->os, NULL);
    if (rc != 0)
        return dino__fail_code(rc);
    if (result)
        *result = t->result;
    dino__thread_release(t);
    return 0;
}

int dino_thread_detach(dino_thread_t thread)
{
    struct dino_thread *t = (struct dino_thread *)thread;
    if (!t)
        return dino__fail(DINO_EINVAL);
    int rc = pthread_detach((pthread_t)t->os);
    if (rc != 0)
        return dino__fail_code(rc);

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
