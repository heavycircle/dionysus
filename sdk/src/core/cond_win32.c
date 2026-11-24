#include <errno.h>
#include <time.h>

#include <dino/core/thread.h>

#include "error_internal.h"

// winpthreads refuses CLOCK_MONOTONIC for condition variables, but its own
// relative wait doesn't follow the wall clock. A file of its own: the SDK's
// other code doesn't use winpthreads, so programs that don't call these
// don't link it.

int dino_cond_init(pthread_cond_t *cond)
{
    if (!cond)
        return dino__fail(DINO_EINVAL);
    int rc = pthread_cond_init(cond, NULL);
    if (rc != 0)
        return dino__fail(rc == ENOMEM ? DINO_ENOMEM : DINO_EOTHER);
    return 0;
}

int dino_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, int timeout_ms)
{
    if (!cond || !mutex || timeout_ms < -1)
        return dino__fail(DINO_EINVAL);
    int rc;
    if (timeout_ms < 0)
        rc = pthread_cond_wait(cond, mutex);
    else
    {
        struct timespec wait = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L };
        rc                   = pthread_cond_timedwait_relative_np(cond, mutex, &wait);
    }
    if (rc == ETIMEDOUT)
        return DINO_TIMEOUT;
    if (rc != 0)
        return dino__fail(rc == EINVAL || rc == EPERM ? DINO_EINVAL : DINO_EOTHER);
    return 0;
}
