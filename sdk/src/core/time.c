#include <dino/core/time.h>

#define NS_PER_US 1000ULL
#define NS_PER_MS 1000000ULL
#define NS_PER_S  1000000000ULL

#ifdef _WIN32

#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// FILETIME counts 100 ns intervals since 1601-01-01.
#define FILETIME_UNIX_EPOCH 116444736000000000ULL

uint64_t dino_time_monotonic_ns(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);

    // Split so count * NS_PER_S can't overflow on long uptimes.
    uint64_t f = (uint64_t)freq.QuadPart;
    uint64_t c = (uint64_t)count.QuadPart;
    return c / f * NS_PER_S + c % f * NS_PER_S / f;
}

uint64_t dino_time_unix_ms(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - FILETIME_UNIX_EPOCH) / 10000;
}

// Sleep() and plain waitable timers wake on the scheduler tick (15.6 ms by
// default) and may return up to a tick early, so wait on a timer and re-arm it
// until the monotonic deadline has really passed.
static void sleep_ns(uint64_t ns)
{
    if (ns == 0)
        return;
    uint64_t deadline = dino_time_monotonic_ns() + ns;

    // The high-resolution flag needs Windows 10 1803; older systems reject it.
    HANDLE timer = CreateWaitableTimerExW(
        NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
        timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);

    for (uint64_t now; (now = dino_time_monotonic_ns()) < deadline;)
    {
        uint64_t remaining = deadline - now;
        if (!timer)
        {
            Sleep((DWORD)((remaining + NS_PER_MS - 1) / NS_PER_MS));
            continue;
        }
        // Negative due times are relative, in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((remaining + 99) / 100);
        if (!SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        {
            CloseHandle(timer);
            timer = NULL;
            continue;
        }
        WaitForSingleObject(timer, INFINITE);
    }

    if (timer)
        CloseHandle(timer);
}

#else

#include <errno.h>
#include <time.h>

uint64_t dino_time_monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NS_PER_S + (uint64_t)ts.tv_nsec;
}

uint64_t dino_time_unix_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / NS_PER_MS;
}

// An absolute deadline makes resuming after EINTR exact: a relative nanosleep
// would drift by however long the signal handler ran.
static void sleep_ns(uint64_t ns)
{
    if (ns == 0)
        return;
    uint64_t deadline = dino_time_monotonic_ns() + ns;

    struct timespec ts;
    ts.tv_sec  = (time_t)(deadline / NS_PER_S);
    ts.tv_nsec = (long)(deadline % NS_PER_S);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR)
        ;
}

#endif // _WIN32

void dino_sleep_ms(uint32_t ms)
{
    sleep_ns((uint64_t)ms * NS_PER_MS);
}

void dino_sleep_us(uint64_t us)
{
    // Saturate (at ~292 years) so the deadline in sleep_ns can't wrap.
    sleep_ns(us > UINT64_MAX / NS_PER_US ? UINT64_MAX / 2 : us * NS_PER_US);
}
