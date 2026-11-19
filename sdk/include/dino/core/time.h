#ifndef DINO_CORE_TIME_H
#define DINO_CORE_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Sleep for at least the given number of milliseconds.
 *
 * Resumes after signals (POSIX) and early wake-ups (Windows) until the full
 * time has passed.
 *
 * @param ms  Milliseconds to sleep. 0 returns immediately.
 */
void dino_sleep_ms(uint32_t ms);

/**
 * Sleep for at least the given number of microseconds.
 *
 * Uses a high-resolution waitable timer on Windows, so short sleeps aren't
 * rounded up to the 15.6 ms scheduler tick where the system supports it.
 *
 * @param us  Microseconds to sleep. 0 returns immediately.
 */
void dino_sleep_us(uint64_t us);

/**
 * Read a monotonic clock.
 *
 * The epoch is arbitrary; only differences between two readings are
 * meaningful. Never goes backwards, even when the wall clock is changed.
 *
 * @return Nanoseconds since an arbitrary fixed point.
 */
uint64_t dino_time_monotonic_ns(void);

/**
 * Read the wall clock.
 *
 * @return Milliseconds since 1970-01-01 00:00:00 UTC.
 */
uint64_t dino_time_unix_ms(void);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_TIME_H
