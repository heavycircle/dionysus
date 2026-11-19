# Threads

`#include <dino/core/thread.h>` · `dino_core`

Start threads, wait for them, or let them run on their own. The API is native threads on both
platforms. On Windows, `pthread_create()` would pull in the winpthreads library, which `dino_thread_*`
doesn't need.

A thread's entry point is an ordinary function, `int fn(void *arg)`, on every platform, with no
calling-convention macros. Its return value comes back from `dino_thread_join`.

## Example: four workers

```c
#include <stdio.h>

#include <dino/core/thread.h>

#define WORKERS 4

struct job
{
    int from, to;
};

static int sum_range(void *arg)
{
    struct job *job = arg;
    int         sum = 0;
    for (int i = job->from; i < job->to; i++)
        sum += i;
    return sum;
}

int main(void)
{
    dino_thread_t threads[WORKERS];
    struct job    jobs[WORKERS];
    for (int i = 0; i < WORKERS; i++)
    {
        jobs[i] = (struct job) { i * 100, (i + 1) * 100 };
        if (dino_thread_create(&threads[i], sum_range, &jobs[i]) != 0)
            return 1;
    }

    int total = 0;
    for (int i = 0; i < WORKERS; i++)
    {
        int part;
        dino_thread_join(threads[i], &part);
        total += part;
    }
    printf("sum of 0..399 = %d\n", total);
    return total == 399 * 400 / 2 ? 0 : 1;
}
```

## Functions

| Function | Does | Returns |
| --- | --- | --- |
| `dino_thread_create(&t, fn, arg)` | Start `fn(arg)` on a new thread. | `0`, `-1` (`t` is then `DINO_INVALID_THREAD`) |
| `dino_thread_join(t, &result)` | Wait for it to finish, store `fn`'s return value (`result` may be `NULL`), release it. | `0`, `-1` for an invalid handle or the calling thread itself |
| `dino_thread_detach(t)` | Let it finish on its own; it's released when it does. | `0`, `-1` |
| `dino_thread_current_id()` | The calling thread's OS thread ID, unique among running threads, for logs. | the ID |
| `dino_thread_yield()` | Give up the rest of the time slice. | |

## Rules

- **Join or detach every thread exactly once.** After either, the handle is invalid.
- **Arguments must outlive the thread.** `arg` is passed as is. Don't point it at a local variable
  that goes out of scope while the thread still runs (the example's `jobs` array lives until the
  joins).
- **Stack size** is the platform default: 8 MB on the Linux targets and 1 MB on Windows. Keep large
  buffers on the heap so code that's fine on Linux doesn't overflow on Windows.
- **Shared data** needs a [mutex](mutex.md) or C11 atomics (`<stdatomic.h>`, which work on every
  target).
- **Process exit:** returning from `main()` ends the process, detached threads included.

See also: [`examples/thread_example`](../../examples/thread_example/main.c).
