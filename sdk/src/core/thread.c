#include <dino/core/thread.h>

#include <stdatomic.h>
#include <stdlib.h>

#include "thread_internal.h"

void dino__thread_release(struct dino_thread *t)
{
    if (atomic_fetch_sub(&t->refs, 1) == 1)
        free(t);
}

void dino__thread_run(struct dino_thread *t)
{
    t->result = t->fn(t->arg);
    dino__thread_release(t);
}

struct dino_thread *dino__thread_alloc(dino_thread_fn fn, void *arg)
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
