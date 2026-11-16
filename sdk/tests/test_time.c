#include <stdint.h>
#include <time.h>
#include <utest.h>

#include <dino/core/time.h>

// Upper bounds only catch unit mix-ups (ms taken as s, us as ms); they're loose
// because qemu and wine on a loaded CI runner can oversleep a lot.
#define SLACK_NS (900ULL * 1000000ULL)

UTEST(time, monotonic_never_goes_backwards)
{
    uint64_t prev = dino_time_monotonic_ns();
    for (int i = 0; i < 100000; i++)
    {
        uint64_t now = dino_time_monotonic_ns();
        ASSERT_GE(now, prev);
        prev = now;
    }
}

UTEST(time, monotonic_brackets_sleep_ms)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_ms(30);
    uint64_t elapsed = dino_time_monotonic_ns() - start;
    EXPECT_GE(elapsed, 30ULL * 1000000ULL);
    EXPECT_LT(elapsed, 30ULL * 1000000ULL + SLACK_NS);
}

UTEST(time, monotonic_brackets_sleep_us)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_us(20000);
    uint64_t elapsed = dino_time_monotonic_ns() - start;
    EXPECT_GE(elapsed, 20000ULL * 1000ULL);
    EXPECT_LT(elapsed, 20000ULL * 1000ULL + SLACK_NS);
}

// Shorter than the Windows scheduler tick: must still not return early.
UTEST(time, sub_millisecond_sleep_is_not_short)
{
    for (int i = 0; i < 5; i++)
    {
        uint64_t start = dino_time_monotonic_ns();
        dino_sleep_us(500);
        EXPECT_GE(dino_time_monotonic_ns() - start, 500ULL * 1000ULL);
    }
}

UTEST(time, zero_sleep_returns_promptly)
{
    uint64_t start = dino_time_monotonic_ns();
    dino_sleep_ms(0);
    dino_sleep_us(0);
    EXPECT_LT(dino_time_monotonic_ns() - start, SLACK_NS);
}

UTEST(time, unix_ms_matches_libc)
{
    time_t   before = time(NULL);
    uint64_t ms     = dino_time_unix_ms();
    time_t   after  = time(NULL);
    // 2023-11-14, well before this module existed.
    EXPECT_GT(ms, 1700000000000ULL);
    EXPECT_GE(ms / 1000, (uint64_t)before);
    EXPECT_LE(ms / 1000, (uint64_t)after);
}

UTEST_MAIN()
