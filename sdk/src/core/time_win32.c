#include <dino/core/time.h>

#include <windows.h>

#include "time_internal.h"

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
void dino__sleep_ns(uint64_t ns)
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
