# Time

`#include <dino/core/time.h>` · `dino_core`

Sleeping and clocks, the same way on every target. `sleep()` counts whole seconds and Windows'
`Sleep()` milliseconds. `clock_gettime()` is POSIX-only and `QueryPerformanceCounter()` Windows-only.
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
    printf("unix time: %llu ms\n", (unsigned long long)dino_time_unix_ms());
    return elapsed_us >= 20500 ? 0 : 1;
}
```

## Functions

| Function | Does |
| --- | --- |
| `void dino_sleep_ms(uint32_t ms)` | Sleep for at least `ms` milliseconds. |
| `void dino_sleep_us(uint64_t us)` | Sleep for at least `us` microseconds. Uses a high-resolution timer on Windows, so short sleeps aren't rounded up to the scheduler tick where the system supports it. |
| `uint64_t dino_time_monotonic_ns(void)` | Nanoseconds since an arbitrary fixed point. Only differences between two readings mean anything. Never goes backwards. |
| `uint64_t dino_time_unix_ms(void)` | Milliseconds since 1970-01-01 00:00:00 UTC, from the wall clock. |

- **Never early:** sleeps resume after signals (Linux) and early wake-ups (Windows) until the full time
  has passed. They can last longer than asked, never shorter. `0` returns at once.
- **Durations and timeouts** come from the monotonic clock. The wall clock can jump when the time is
  set or synchronized.
- **Calendar dates:** `dino_time_unix_ms() / 1000` is a `time_t` value for `gmtime` and `strftime`
  from `time.h`.

See also: [`examples/sleep_example`](../../examples/sleep_example/main.c).
