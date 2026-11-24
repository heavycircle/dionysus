# Time

`#include <dino/core/time.h>` · `dino_core`

Sleeping and clocks, the same way on every target. `sleep()` counts whole seconds and Windows'
`Sleep()` milliseconds. `usleep()` and `nanosleep()` exist on Windows too, but round down to whole milliseconds there. `clock_gettime()` works everywhere, but on Windows it pulls in winpthreads, about 25 KB more than `dino_time_monotonic_ns()`.
Windows' default timer also rounds short sleeps up to its 15.6 ms tick.

## Example: timing a sleep

```c
#include <stdio.h>

#include <dino/core/time.h>

int main(void)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_ms(20);
    dino_sleep_us(500);
    uint64_t elapsed_us = (dino_time_monotonic_ns() - start) / 1000;

    printf("slept %llu us\n", (unsigned long long)elapsed_us);
    return elapsed_us >= 20500 ? 0 : 1;
}
```

## Functions

| Function                                | Does                                                                                                                                                                 |
| --------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `void dino_sleep_ms(uint32_t ms)`       | Sleep for at least `ms` milliseconds.                                                                                                                                |
| `void dino_sleep_us(uint64_t us)`       | Sleep for at least `us` microseconds. Uses a high-resolution timer on Windows, so short sleeps aren't rounded up to the scheduler tick where the system supports it. |
| `uint64_t dino_time_monotonic_ns(void)` | Nanoseconds since an arbitrary fixed point. Only differences between two readings mean anything. Never goes backwards.                                               |

- **Never early:** sleeps resume after signals (Linux) and early wake-ups (Windows) until the full time
  has passed. They can last longer than asked, never shorter. `0` returns at once.
- **Suspend:** the monotonic clock stops while a Linux machine is suspended and keeps counting on Windows, and sleeps and timeouts follow it: `dino_sleep_ms(60000)` across an hour of suspend returns at once on resume on Windows, and after the time still left on Linux.
- **Durations and timeouts** come from the monotonic clock. The wall clock can jump when the time is
  set or synchronized.
- **Wall-clock time** comes from the C library, the same on every target: `time()` or C11 `timespec_get(&ts, TIME_UTC)`, then `gmtime_r`/`localtime_r` and `strftime` for calendar dates (see [dino vs libc](libc.md)).

See also: [`examples/sleep_example`](../../examples/sleep_example/main.c).
