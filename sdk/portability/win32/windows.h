#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <windows.h>

#include "dino_portability.h"

// WINBASEAPI keeps the redeclarations dllimport, like the originals.
#if defined(DINO__WARNINGS) && !defined(DINO__WINDOWS_H) && (defined(_WINDOWS_) || defined(_INC_WINDOWS))
#define DINO__WINDOWS_H

WINBASEAPI __typeof__(Sleep) Sleep __attribute__((deprecated(
    "Sleep() is Windows-only; use dino_sleep_ms() from <dino/core/time.h>")));
WINBASEAPI __typeof__(CreateThread) CreateThread __attribute__((deprecated(
    "CreateThread() is Windows-only; use pthread_create() from <pthread.h>, which works on every target")));
WINBASEAPI __typeof__(CreatePipe) CreatePipe __attribute__((deprecated(
    "CreatePipe() is Windows-only and its pipes can't be waited on; use dino_pipe_create() from <dino/core/pipe.h>")));
WINBASEAPI __typeof__(CreateProcessA) CreateProcessA __attribute__((deprecated(
    "CreateProcessA() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
WINBASEAPI __typeof__(CreateProcessW) CreateProcessW __attribute__((deprecated(
    "CreateProcessW() is Windows-only; use dino_process_spawn() from <dino/core/process.h>")));
WINBASEAPI __typeof__(InitializeCriticalSection) InitializeCriticalSection __attribute__((deprecated(
    "InitializeCriticalSection() is Windows-only; use pthread_mutex_init() from <pthread.h>, which works on every target")));
WINBASEAPI __typeof__(InitializeCriticalSectionAndSpinCount) InitializeCriticalSectionAndSpinCount __attribute__((deprecated(
    "InitializeCriticalSectionAndSpinCount() is Windows-only; use pthread_mutex_init() from <pthread.h>, which works on every target")));
WINBASEAPI __typeof__(QueryPerformanceCounter) QueryPerformanceCounter __attribute__((deprecated(
    "QueryPerformanceCounter() is Windows-only; use clock_gettime(CLOCK_MONOTONIC) from <time.h>, or dino_time_monotonic_ns() from <dino/core/time.h>, which is about 25 KB smaller on Windows")));

WINBASEAPI __typeof__(TerminateProcess) TerminateProcess __attribute__((deprecated(
    "TerminateProcess() is Windows-only; use dino_process_kill() from <dino/core/process.h>")));
WINBASEAPI __typeof__(GetCurrentProcessId) GetCurrentProcessId __attribute__((deprecated(
    "GetCurrentProcessId() is Windows-only; use getpid() from <unistd.h>, which works on every target")));
WINBASEAPI __typeof__(GetCurrentThreadId) GetCurrentThreadId __attribute__((deprecated(
    "GetCurrentThreadId() is Windows-only; use dino_thread_current_id() from <dino/core/thread.h>")));
WINBASEAPI __typeof__(SwitchToThread) SwitchToThread __attribute__((deprecated(
    "SwitchToThread() is Windows-only; use sched_yield() from <sched.h>, which works on every target")));
WINBASEAPI __typeof__(SleepEx) SleepEx __attribute__((deprecated(
    "SleepEx() is Windows-only; use dino_sleep_ms() from <dino/core/time.h>")));
WINBASEAPI __typeof__(GetEnvironmentVariableA) GetEnvironmentVariableA __attribute__((deprecated(
    "GetEnvironmentVariableA() is Windows-only; use getenv() from <stdlib.h>")));
WINBASEAPI __typeof__(GetEnvironmentVariableW) GetEnvironmentVariableW __attribute__((deprecated(
    "GetEnvironmentVariableW() is Windows-only; use getenv() from <stdlib.h>")));
WINBASEAPI __typeof__(SetEnvironmentVariableA) SetEnvironmentVariableA __attribute__((deprecated(
    "SetEnvironmentVariableA() is Windows-only; use setenv() or unsetenv() from <stdlib.h>, which work on every target")));
WINBASEAPI __typeof__(SetEnvironmentVariableW) SetEnvironmentVariableW __attribute__((deprecated(
    "SetEnvironmentVariableW() is Windows-only; use setenv() or unsetenv() from <stdlib.h>, which work on every target")));
WINBASEAPI __typeof__(GetSystemTimeAsFileTime) GetSystemTimeAsFileTime __attribute__((deprecated(
    "GetSystemTimeAsFileTime() is Windows-only; use timespec_get(&ts, TIME_UTC) from <time.h>, which works on every target")));
WINBASEAPI __typeof__(GetFileSizeEx) GetFileSizeEx __attribute__((deprecated(
    "GetFileSizeEx() is Windows-only; use dino_file_size() from <dino/core/fs.h>")));
WINBASEAPI __typeof__(CreateIoCompletionPort) CreateIoCompletionPort __attribute__((deprecated(
    "CreateIoCompletionPort() is Windows-only; use dino_event_loop_create() from <dino/core/event.h>")));
WINBASEAPI __typeof__(GetQueuedCompletionStatus) GetQueuedCompletionStatus __attribute__((deprecated(
    "GetQueuedCompletionStatus() is Windows-only; use dino_event_wait() from <dino/core/event.h>")));
// Vista and later: declared only when _WIN32_WINNT says so.
#if _WIN32_WINNT >= 0x0600
WINBASEAPI __typeof__(GetTickCount64) GetTickCount64 __attribute__((deprecated(
    "GetTickCount64() is Windows-only; use clock_gettime(CLOCK_MONOTONIC) from <time.h>, or dino_time_monotonic_ns() from <dino/core/time.h>, which is about 25 KB smaller on Windows")));
WINBASEAPI __typeof__(GetQueuedCompletionStatusEx) GetQueuedCompletionStatusEx __attribute__((deprecated(
    "GetQueuedCompletionStatusEx() is Windows-only; use dino_event_wait() from <dino/core/event.h>")));
WINBASEAPI __typeof__(InitOnceExecuteOnce) InitOnceExecuteOnce __attribute__((deprecated(
    "InitOnceExecuteOnce() is Windows-only; use pthread_once() from <pthread.h>, which works on every target")));
WINBASEAPI __typeof__(InitializeConditionVariable) InitializeConditionVariable __attribute__((deprecated(
    "InitializeConditionVariable() is Windows-only; use pthread_cond_init() from <pthread.h>, which works on every target")));
WINBASEAPI __typeof__(SleepConditionVariableCS) SleepConditionVariableCS __attribute__((deprecated(
    "SleepConditionVariableCS() is Windows-only; use pthread_cond_wait() from <pthread.h>, which works on every target")));
#endif

#endif
