#include <errno.h>
#include <time.h>

#include <dino/core/thread.h>

#include "error_internal.h"

int dino_cond_init(pthread_cond_t *cond)
{
    if (!cond)
        return dino__fail(DINO_EINVAL);
    pthread_condattr_t attr;
    int                rc = pthread_condattr_init(&attr);
    if (rc == 0)
    {
        rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        if (rc == 0)
            rc = pthread_cond_init(cond, &attr);
        pthread_condattr_destroy(&attr);
    }
    if (rc != 0)
    {
        errno = rc;
        return dino__fail_os();
    }
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
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += timeout_ms / 1000;
        deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L)
        {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        rc = pthread_cond_timedwait(cond, mutex, &deadline);
    }
    if (rc == ETIMEDOUT)
        return DINO_TIMEOUT;
    if (rc != 0)
    {
        errno = rc;
        return dino__fail_os();
    }
    return 0;
}
