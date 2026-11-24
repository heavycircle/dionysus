# Portability warnings

Calling `pipe()`, `fork()`, `usleep()` or `Sleep()` directly compiles fine on the platform
you test on and then breaks, or quietly behaves differently, on the other one. When the SDK has a
portable replacement, code that links it is told so at the call site:

```
src/main.c:7:9: warning: 'pipe' is deprecated: pipe() is POSIX-only; use dino_pipe_create() from <dino/core/pipe.h> [-Wdeprecated-declarations]
    7 |     if (pipe(fds) != 0)
      |         ^
```

The warning shows up everywhere the code is compiled or analyzed:

- **`dino build`, `dino run` and `dino test`**, for every target.
- **Editors using clangd**, because `dino init` and `dino build` write a `compile_commands.json` at
  the project root.
- **clang-tidy**, as `clang-diagnostic-deprecated-declarations`.

The calls are marked deprecated _for dino projects_: the functions are fine in general, but each has
a `dino_*` replacement that works the same on every target. A deprecation is used, rather than a
custom warning, because the compiler keeps it quiet when the call is inside a system header, such
as an inline function in a header-only library you include. You can't change that code, so it never
warns. A macro such a header defines is different: it expands in your code, so a flagged call inside it warns there.

It doesn't depend on which clangd or clang-tidy version is installed (see
[Tool versions](#tool-versions)). It's a warning, never an error, unless the project builds with
`-Werror`.

- [What's flagged](#whats-flagged)
- [Opting out](#opting-out)
- [Tool versions](#tool-versions)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)

## What's flagged

Only calls that have a `dino_*` replacement, on the platform where the call exists:

| Calls                                                                                                                                                  | Platform | Use instead                                                                                                                  |
| ------------------------------------------------------------------------------------------------------------------------------------------------------ | -------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `fork`, `execv`, `execvp`, `execve`, `execl`, `execlp`, `execle`, `posix_spawn`, `posix_spawnp`                                                        | Linux    | `dino_process_spawn` (`dino/core/process.h`)                                                                                 |
| `waitpid`, `wait`, `waitid`, `wait4`                                                                                                                   | Linux    | `dino_process_wait`                                                                                                          |
| `kill`                                                                                                                                                 | Linux    | `dino_process_kill`                                                                                                          |
| `CreateProcessA`, `CreateProcessW`, `_spawn*`, `_exec*`, `_cwait`                                                                                      | Windows  | `dino_process_spawn`, `dino_process_wait`                                                                                    |
| `TerminateProcess`, `GetCurrentProcessId`, `_getpid`                                                                                                   | Windows  | `dino_process_kill`, `getpid` (which works on every target)                                                                  |
| `putenv`, `clearenv`                                                                                                                                   | Linux    | `setenv`, `unsetenv` (`stdlib.h`, on every target), a full environment for `dino_process_spawn`                              |
| `_putenv`, `GetEnvironmentVariableA/W`, `SetEnvironmentVariableA/W`                                                                                    | Windows  | `getenv`, `setenv`, `unsetenv` (`stdlib.h`, on every target)                                                                 |
| `pipe`, `pipe2`                                                                                                                                        | Linux    | `dino_pipe_create` (`dino/core/pipe.h`)                                                                                      |
| `CreatePipe`                                                                                                                                           | Windows  | `dino_pipe_create`                                                                                                           |
| `pthread_condattr_setclock`                                                                                                                            | both     | `dino_cond_init`, `dino_cond_timedwait` (`dino/core/thread.h`): Windows refuses `CLOCK_MONOTONIC`                            |
| `thrd_*`, `mtx_*`, `cnd_*`, `call_once` (C11 `<threads.h>`, which MinGW lacks)                                                                         | Linux    | pthreads, which work on every target                                                                                         |
| `gettid`                                                                                                                                               | Linux    | `dino_thread_current_id`                                                                                                     |
| `CreateThread`, `_beginthread`, `_beginthreadex`, `GetCurrentThreadId`, `SwitchToThread`                                                               | Windows  | `pthread_create`, `dino_thread_current_id`, `sched_yield`                                                                    |
| `InitializeCriticalSection`, `InitializeCriticalSectionAndSpinCount`, `InitializeConditionVariable`, `SleepConditionVariableCS`, `InitOnceExecuteOnce` | Windows  | `pthread_mutex_*`, `pthread_cond_*`, `pthread_once`                                                                          |
| `socket`, `accept`, `connect`, `send`, `recvfrom`                                                                                                      | both     | `dino_socket_*` (`dino/core/network.h`)                                                                                      |
| `accept4`                                                                                                                                              | Linux    | `dino_socket_accept`                                                                                                         |
| `closesocket`, `ioctlsocket`, `WSASocketA/W`, `WSAStartup`, `WSACleanup`, `WSAGetLastError`                                                            | Windows  | `dino_socket_close`, `dino_socket_set_nonblocking`, `dino_socket_create`, `dino_net_init`, nothing, `dino_socket_last_error` |
| `ppoll`, `pselect`                                                                                                                                     | Linux    | `poll`, `select`, which work on every target                                                                                 |
| `WSAPoll`                                                                                                                                              | Windows  | `poll`                                                                                                                       |
| `epoll_create`, `epoll_create1`, `epoll_ctl`, `epoll_wait`, `epoll_pwait`                                                                              | Linux    | the event loop (`dino/core/event.h`)                                                                                         |
| `CreateIoCompletionPort`, `GetQueuedCompletionStatus`, `GetQueuedCompletionStatusEx`                                                                   | Windows  | the event loop                                                                                                               |
| `usleep`                                                                                                                                               | both     | `dino_sleep_us` (`dino/core/time.h`): Windows rounds down to whole milliseconds                                              |
| `nanosleep`, `clock_nanosleep`                                                                                                                         | Linux    | `dino_sleep_us`: Windows rounds down to whole milliseconds                                                                   |
| `Sleep`, `SleepEx`, `QueryPerformanceCounter`, `GetTickCount64`, `GetSystemTimeAsFileTime`                                                             | Windows  | `dino_sleep_ms`, `clock_gettime` or `dino_time_monotonic_ns` (25 KB smaller on Windows), `timespec_get`                      |
| `GetFileSizeEx`                                                                                                                                        | Windows  | `dino_file_size` (`dino/core/fs.h`)                                                                                          |

The socket calls are flagged whether they come from `<winsock2.h>` or from the Winsock 1 that `<windows.h>` includes without `WIN32_LEAN_AND_MEAN`.

Calls that behave the same on every target aren't flagged, even where the SDK has a similar `dino_*` call: `pthread_*` (winpthreads on Windows, linked statically, about 8–10 KB), `sched_yield`, `getpid`, `sleep`, `gettimeofday`, `clock_gettime` (which pulls in winpthreads on Windows, about 25 KB more than `dino_time_monotonic_ns`), and `bind`, `listen`, `recv`, `sendto`, `setsockopt`, `shutdown`, `poll` and `select`, and `setenv` and `unsetenv`, which the SDK defines on Windows.

These stay quiet too:

- `read`, `write` and `close`, which are right on files, and nothing at compile time can tell a file
  descriptor from a pipe or a socket.
- Calls with no dino replacement, such as `pthread_sigmask`, `sigaction`, `getaddrinfo` and `getenv` (which reads the environment the same way everywhere).
- `usleep`, `pipe2`, `nanosleep`, `kill`, `setenv` and the other POSIX additions in strict language modes (`-std=c11` without
  feature macros), where the C library doesn't declare them anyway.
- `poll` in a glibc build with `_FORTIFY_SOURCE` (the `dino test --memcheck` build doesn't use it): glibc then calls them through checking versions of its own.

A call inside a **macro** from a system header is a different case from an inline function: the macro expands in your code, so the compiler reports the call there, and under `-Werror` it fails the build. Wrap the macro's use, `DINO_PLATFORM_CALL(VENDOR_FORK())`, or turn the warning off for that file.

## Opting out

Most code should switch to the `dino_*` call. When a raw call is deliberate, there are four levels
of opt-out, from the whole project down to one call.

**A whole project.** Set `DINO_NO_PLATFORM_WARNINGS` before `project()`, which is where dino adds
the SDK:

```cmake
cmake_minimum_required(VERSION 3.20)
set(DINO_NO_PLATFORM_WARNINGS ON)
project(app C)
```

or pass it when building: `dino build -- -DDINO_NO_PLATFORM_WARNINGS=ON`. (A `-D` stays in
`CMakeCache.txt` until it's changed, so the `set()` form is easier to keep track of.)

**One target.** Define `DINO_ALLOW_PLATFORM_CALLS` for it:

```cmake
target_compile_definitions(legacy_tool PRIVATE DINO_ALLOW_PLATFORM_CALLS)
```

**One file.** Define it as `1` before the first `#include`:

```c
#define DINO_ALLOW_PLATFORM_CALLS 1
#include <pthread.h>
#include <unistd.h>
```

Use the value `1`: that's the value the CMake forms give it, so a file that has the line still
builds without a redefinition warning when the whole project opts out too.

**One call.** Wrap it in `DINO_PLATFORM_CALL()`, which every flagged header defines:

```c
#include <unistd.h>

pid_t pid = DINO_PLATFORM_CALL(fork());
DINO_PLATFORM_CALL(execvp(argv[0], argv));
```

It works around statements and expressions alike, and silences only the calls inside it.

**Compiler flags** work too, as with any deprecation: `-Wno-deprecated-declarations` turns them
off, and `-Werror=deprecated-declarations` turns them into errors for a project that wants to forbid
raw calls outright. Both also apply to every other deprecated function the project uses, which is
why the opt-outs above are usually the better choice.

### With `-Werror`

A project that builds with `-Werror` (CI often does) fails on these warnings. That's usually what
you want. To keep `-Werror` for everything else while the calls are being replaced, add
`-Wno-error=deprecated-declarations`.

## Tool versions

The warnings come from the compiler, not from a clang-tidy check:

- **`dino build`** always compiles with the clang bundled in Zig (clang 22 with Zig 0.17.1),
  whatever compiler the host has.
- **clangd and clang-tidy** are clang frontends reading the same `compile_commands.json`, so they
  report the same warning at any version. clang-tidy 14 (Ubuntu 22.04) and 19 were both checked.
- **clang-tidy** calls it `clang-diagnostic-deprecated-declarations`. A `Checks` list that starts
  with `-*` hides compiler warnings, so the `.clang-tidy` that `dino init` writes names this one
  explicitly. Add it to your own config if you keep a different one. clangd shows compiler warnings
  whatever `Checks` says.

The `.clang-tidy` that `dino init` writes uses the list form of `CheckOptions`, which clang-tidy and
clangd 14 can read. The shorter map form makes those versions ignore the whole file.

clang-tidy 20 added a `CustomFunctions` option to `bugprone-unsafe-functions` that could flag the
same calls. dino doesn't use it: clang-tidy 19 and older silently ignore it, and it would leave
`dino build` silent.

## How it works

`sdk/portability/` holds a small wrapper for each system header that declares a flagged call: `unistd.h`, `pthread.h` and `stdlib.h` on every target (`common/`); `poll.h`, `signal.h`, `spawn.h`, `sys/epoll.h`, `sys/select.h`, `sys/socket.h`, `sys/wait.h`, `threads.h` and `time.h` on Linux (`posix/`); and `windows.h`, `winsock.h`, `winsock2.h` and `process.h` on Windows (`win32/`). Each wrapper includes
the real header with `#include_next`, then redeclares the calls with a warning attached:

```c
#pragma GCC system_header
#include_next <unistd.h>
...
__typeof__(pipe) pipe __attribute__((deprecated(
    "pipe() is POSIX-only; use dino_pipe_create() from <dino/core/pipe.h>")));
```

- **`__typeof__(pipe)`** copies the C library's exact prototype, so the same wrapper fits musl,
  glibc (the `dino test --memcheck` build) and MinGW-w64. On Windows the redeclarations keep their
  `dllimport` linkage.
- **`deprecated`** makes the compiler warn wherever the function is used in your code, but not
  inside system headers. clang's `diagnose_if` attribute would allow a custom warning flag, but
  clang shows it even inside system headers, so a header-only library you include could produce
  warnings you can't fix.
- **`#pragma GCC system_header`** keeps the wrapper itself out of warnings and out of clang-tidy's
  results.
- **Nothing else changes.** A wrapper takes effect wherever that header is included, by your code or by another header (musl's `<sys/types.h>` includes `<sys/select.h>`, for example). It adds no declarations and no includes of its own. Linux-only wrappers are only on the include path for Linux targets and Windows-only ones only for Windows, so `__has_include` still gives the right answer. (`win32-posix/` is separate: the POSIX socket and `poll.h` headers the SDK provides on Windows, which MinGW lacks, so `__has_include(<poll.h>)` is true there too.)
- **When unsure, no warning.** A redeclaration that depends on feature macros (`usleep`, `pipe2`,
  the POSIX clocks) is only made when the header surely declared the function, and nothing is redeclared unless the C library's own header was included: a header of your own with the same name, such as a library's `process.h` found through the wrapper, is passed through untouched. A wrapper can miss a warning, but it can't break a build.

The SDK adds the directory to `dino_core`'s interface include path. Everything that links the SDK
gets it: `dino_executable()`, `dino_test()`, a plain `target_link_libraries(app PRIVATE dino_core)`,
and [organization libraries](libraries.md). The SDK's own sources build with `DINO_ALLOW_PLATFORM_CALLS`, so they never warn.

`tests/warnings.sh` checks all of this in CI on every target. It builds
[`tests/fixtures/raw_calls`](../../tests/fixtures/raw_calls), which calls every flagged function, in
four language modes, under clang's `-verify`: every call must produce its exact warning, and nothing
else may warn, including flagged calls inside a system header's inline functions. It then builds the fixture again with the project opt-out, where nothing may warn.

## Troubleshooting

| Symptom                                       | Cause                                                                                                                                                                              |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A call you expected to be flagged isn't       | It's in a header dino doesn't wrap, the file defines `DINO_ALLOW_PLATFORM_CALLS`, or the language mode hides the declaration (see [What's flagged](#whats-flagged)).               |
| `'DINO_ALLOW_PLATFORM_CALLS' macro redefined` | A file defines it without a value while the target or project defines it too. Write `#define DINO_ALLOW_PLATFORM_CALLS 1`.                                                         |
| No warnings at all, even in `dino build`      | The project builds with `-Wno-deprecated-declarations`, which hides these along with every other deprecation. Remove it, or use the opt-outs above for the calls you mean to keep. |
| The editor doesn't show the warnings          | clangd can't find `compile_commands.json`. Run `dino build` once; it writes the file to the project root.                                                                          |
| A build error inside `sdk/portability/`       | A bug: please open an issue with the target and the `-std` / feature macros in use.                                                                                                |
