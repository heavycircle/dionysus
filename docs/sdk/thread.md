# Threads

`#include <dino/core/thread.h>` · `dino_core`

Threads, mutexes and condition variables are plain **pthreads** on every target: musl's on Linux and winpthreads on Windows, which zig links statically with no flag. Write `pthread_create`, `pthread_mutex_lock`, `pthread_cond_wait`, `pthread_once` and C11 atomics as on Linux. They behave the same on all seven targets.

The SDK adds two things pthreads can't do the same way everywhere: a condition-variable wait with a timeout that follows the monotonic clock, and the calling thread's OS ID.

## Example: wait for workers, with a timeout

```c
#include <pthread.h>
#include <stdio.h>

#include <dino/core/thread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  changed;
static int             done = 0;

static void *worker(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&lock);
    done++;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    if (dino_cond_init(&changed) != 0)
        return 1;
    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        if (pthread_create(&threads[i], NULL, worker, NULL) != 0)
            return 1;

    int timed_out = 0;
    pthread_mutex_lock(&lock);
    while (done < 4 && !timed_out)
        timed_out = dino_cond_timedwait(&changed, &lock, 5000) == DINO_TIMEOUT;
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < 4; i++)
        pthread_join(threads[i], NULL);
    pthread_cond_destroy(&changed);
    printf("%d workers done%s\n", done, timed_out ? " (timed out)" : "");
    return timed_out ? 1 : 0;
}
```

## Functions

| Function                                 | Does                                                                                                                                                                                          | Returns                                       |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------- |
| `dino_cond_init(&cond)`                  | Initialize a `pthread_cond_t` whose timed waits follow the monotonic clock, in place of `pthread_cond_init`. Signal, broadcast, plain waits and `pthread_cond_destroy` are the pthread calls. | `0`, `-1`                                     |
| `dino_cond_timedwait(&cond, &mutex, ms)` | Like `pthread_cond_wait`, but give up after `ms` milliseconds (`-1`: never, `0`: check once). The caller holds `mutex`; wakeups can be spurious, so loop on the condition.                    | `0` when woken, `DINO_TIMEOUT`, `-1` on error |
| `dino_thread_current_id()`               | The calling thread's OS thread ID (`gettid`, `GetCurrentThreadId`), unique among running threads, for logs.                                                                                   | the ID                                        |

## Why these two

- **Timed waits:** `pthread_cond_timedwait` takes an absolute deadline on the wall clock unless the condition variable was set to `CLOCK_MONOTONIC` with `pthread_condattr_setclock`, which Windows refuses (`EINVAL`). On the wall clock, setting the time moves the deadline. `dino_cond_timedwait` uses the monotonic clock on Linux and winpthreads' relative wait on Windows.
- **Thread IDs:** `pthread_t` isn't an integer on every target, and `gettid` is Linux-only.

## Notes

- **Size on Windows:** winpthreads adds about 8–10 KB to a Windows program that uses pthreads. The SDK's own code uses the Win32 API directly, so a program that never calls pthreads doesn't carry it.
- **Stack size:** a thread's default stack is 128 KiB with musl and 1 MiB on Windows, so deep recursion that works on Windows can overflow on Linux. Set it with `pthread_attr_setstacksize`, or keep large buffers on the heap.
- **Thread-local storage:** `_Thread_local` works on every target.
- **C11 `<threads.h>`** exists on Linux only; MinGW has none. Use pthreads.

See also: [`examples/thread_example`](../../examples/thread_example/main.c), [`examples/mutex_example`](../../examples/mutex_example/main.c).
