// Sleeps too long to ever end, each in a thread that's still asleep when the
// test exits. Its own executable, run without the leak check (see
// CMakeLists.txt): those threads keep their handles.
#include <stdatomic.h>
#include <stdint.h>
#include <utest.h>

#include <dino/core/thread.h>
#include <dino/core/time.h>

static const uint64_t g_durations[] = {
    UINT64_MAX,
    UINT64_MAX / 1000,        // nanoseconds just under UINT64_MAX: the deadline wrapped
    UINT64_MAX / 1000 + 1,    // the first value the old saturation caught
    UINT64_MAX / 2000 + 1000, // past half the clock's range
};
static atomic_int g_woke;

static int sleep_for(void *arg)
{
    dino_sleep_us(*(const uint64_t *)arg);
    atomic_fetch_add(&g_woke, 1);
    return 0;
}

UTEST(sleep, huge_durations_dont_wrap_to_short_sleeps)
{
    for (size_t i = 0; i < sizeof(g_durations) / sizeof(g_durations[0]); i++)
    {
        dino_thread_t t;
        ASSERT_EQ(dino_thread_create(&t, sleep_for, (void *)&g_durations[i]), 0);
        ASSERT_EQ(dino_thread_detach(t), 0);
    }
    dino_sleep_ms(200);
    ASSERT_EQ(atomic_load(&g_woke), 0);
}

UTEST_MAIN()
