// clang-format off: one redeclaration per call, laid out to be read.
#pragma GCC system_header
#include_next <windows.h>

#include "dino_portability.h"

// WINBASEAPI keeps the redeclarations dllimport, like the originals.
#if defined(DINO__WARNINGS) && !defined(DINO__WINDOWS_H)
#define DINO__WINDOWS_H

WINBASEAPI __typeof__(Sleep) Sleep __attribute__((deprecated(
    "Sleep() is Windows-only; use dino_sleep_ms() from <dino/core/time.h>")));
WINBASEAPI __typeof__(CreateThread) CreateThread __attribute__((deprecated(
    "CreateThread() is Windows-only; use dino_thread_create() from <dino/core/thread.h>")));
WINBASEAPI __typeof__(CreatePipe) CreatePipe __attribute__((deprecated(
    "CreatePipe() is Windows-only and its pipes can't be waited on; use dino_pipe_create() from <dino/core/pipe.h>")));
WINBASEAPI __typeof__(CreateProcessA) CreateProcessA __attribute__((deprecated(
    "CreateProcessA() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
WINBASEAPI __typeof__(CreateProcessW) CreateProcessW __attribute__((deprecated(
    "CreateProcessW() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
WINBASEAPI __typeof__(InitializeCriticalSection) InitializeCriticalSection __attribute__((deprecated(
    "InitializeCriticalSection() is Windows-only; use dino_mutex_create() from <dino/core/mutex.h>")));
WINBASEAPI __typeof__(InitializeCriticalSectionAndSpinCount) InitializeCriticalSectionAndSpinCount __attribute__((deprecated(
    "InitializeCriticalSectionAndSpinCount() is Windows-only; use dino_mutex_create() from <dino/core/mutex.h>")));
WINBASEAPI __typeof__(QueryPerformanceCounter) QueryPerformanceCounter __attribute__((deprecated(
    "QueryPerformanceCounter() is Windows-only; use dino_time_monotonic_ns() from <dino/core/time.h>")));

#endif
