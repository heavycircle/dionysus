# Mutexes, condition variables and run-once

`#include <dino/core/mutex.h>` · `dino_core`

Mutexes, condition variables and one-time initialization, built on native primitives on both
platforms (pthreads on Linux; critical sections and condition variables on Windows). Like
[threads](thread.md), they don't need winpthreads on Windows.

## Example: a producer and a consumer

```c
#include <stdio.h>

#include <dino/core/mutex.h>
#include <dino/core/thread.h>

static dino_mutex_t lock;
static dino_cond_t  changed;
static int          queue[16];
static int          count, finished;

static int producer(void *arg)
{
    (void)arg;
    for (int i = 1; i <= 10; i++)
    {
        dino_mutex_lock(lock);
        queue[count++] = i;
        dino_cond_signal(changed);
        dino_mutex_unlock(lock);
    }
    dino_mutex_lock(lock);
    finished = 1;
    dino_cond_signal(changed);
    dino_mutex_unlock(lock);
    return 0;
}

int main(void)
{
    lock    = dino_mutex_create();
    changed = dino_cond_create();
    if (lock == DINO_INVALID_MUTEX || changed == DINO_INVALID_COND)
        return 1;

    dino_thread_t t;
    if (dino_thread_create(&t, producer, NULL) != 0)
        return 1;

    int sum = 0;
    dino_mutex_lock(lock);
    for (;;)
    {
        while (count == 0 && !finished) // wake-ups can be spurious: always re-check
            dino_cond_wait(changed, lock);
        while (count > 0)
            sum += queue[--count];
        if (finished)
            break;
    }
    dino_mutex_unlock(lock);

    dino_thread_join(t, NULL);
    dino_cond_destroy(changed);
    dino_mutex_destroy(lock);
    printf("consumed a total of %d\n", sum);
    return sum == 55 ? 0 : 1;
}
```

## Mutexes

| Function                | Does                                      | Returns                                    |
| ----------------------- | ----------------------------------------- | ------------------------------------------ |
| `dino_mutex_create()`   | A new mutex.                              | the handle, or `DINO_INVALID_MUTEX`        |
| `dino_mutex_destroy(m)` | Free it. It must not be locked.           |                                            |
| `dino_mutex_lock(m)`    | Lock, waiting if another thread holds it. | `0`, `-1` on error                         |
| `dino_mutex_trylock(m)` | Lock only if that's possible right now.   | `0` if locked, `-1` if it's held elsewhere |
| `dino_mutex_unlock(m)`  | Unlock.                                   | `0`, `-1` on error                         |

Mutexes are **recursive**: the thread that holds one can lock it again, and must unlock it as many
times. That's the behaviour of Windows critical sections, and the SDK gives it on Linux too, so code
doesn't deadlock on one platform only.

## Condition variables

| Function                                      | Does                                                  | Returns                              |
| --------------------------------------------- | ----------------------------------------------------- | ------------------------------------ |
| `dino_cond_create()` / `dino_cond_destroy(c)` | Create, or free (no thread may be waiting).           | the handle, or `DINO_INVALID_COND`   |
| `dino_cond_wait(c, m)`                        | Unlock `m`, wait for a signal, lock `m` again.        | `0`, `-1`                            |
| `dino_cond_timedwait(c, m, timeout_ms)`       | The same, giving up after `timeout_ms` (`-1`: never). | `0` when woken, `DINO_TIMEOUT`, `-1` |
| `dino_cond_signal(c)`                         | Wake one waiting thread.                              | `0`, `-1`                            |
| `dino_cond_broadcast(c)`                      | Wake all of them.                                     | `0`, `-1`                            |

- **Re-check in a loop:** wake-ups can be spurious, so always wait in a loop that re-checks the
  condition, as the example does.
- **Lock once before waiting:** the mutex must be locked exactly once by the waiting thread. Waiting
  while holding it more than once is undefined.
- **Monotonic timeouts:** `dino_cond_timedwait` measures on the monotonic clock, so changing the
  system time doesn't shorten or extend it.

## Run once

```
static dino_once_t once = DINO_ONCE_INIT;
static void init_tables(void) { ... }

dino_once(&once, init_tables);   // from any thread, any number of times
```

`dino_once` runs the function exactly once per flag, however many threads call it, and every call
returns only after it has finished, so what it set up is ready to use. The function must not call
`dino_once` on the same flag. Returns `0`, or `-1` for a `NULL` argument.

See also: [`examples/mutex_example`](../../examples/mutex_example/main.c).
