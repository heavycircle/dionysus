# The dino SDK

The `dino_*` API is a small C portability layer over the places where Linux (musl) and Windows
(MinGW-w64) disagree. Code written against it builds and behaves the same on all seven targets.
Everywhere else (strings, memory, `printf`, files through `stdio.h`) you use the C library as
usual; [dino vs libc](libc.md) says which is which.

Every project `dino` builds can use it: `dino_executable()` links it, or link it yourself with
`target_link_libraries(app PRIVATE dino_core)` (or `dino_util` for logging too).

## Modules

| Module                   | Header                | Library     | For                                                           |
| ------------------------ | --------------------- | ----------- | ------------------------------------------------------------- |
| [Runtime](runtime.md)    | `dino/core/runtime.h` | `dino_core` | process-wide setup, which is automatic                        |
| [Network](network.md)    | `dino/core/network.h` | `dino_core` | TCP and UDP sockets over IPv4 and IPv6                        |
| [Poll and select](io.md) | `dino/core/io.h`      | `dino_core` | waiting on a few sockets at once                              |
| [Event loop](event.md)   | `dino/core/event.h`   | `dino_core` | waiting on many sockets, pipes, processes and timers at once  |
| [Threads](thread.md)     | `dino/core/thread.h`  | `dino_core` | starting, joining and detaching threads                       |
| [Mutexes](mutex.md)      | `dino/core/mutex.h`   | `dino_core` | mutexes, condition variables, run-once                        |
| [Pipes](pipe.md)         | `dino/core/pipe.h`    | `dino_core` | byte pipes between threads and processes                      |
| [Processes](process.md)  | `dino/core/process.h` | `dino_core` | running programs, their stdio and exit codes; the environment |
| [Files](fs.md)           | `dino/core/fs.h`      | `dino_core` | file sizes over 2 GB; stdio for programs the kernel starts    |
| [Time](time.md)          | `dino/core/time.h`    | `dino_core` | sleeping, a monotonic clock, the wall clock                   |
| [Logging](logging.md)    | `dino/util/logging.h` | `dino_util` | levelled log lines on stderr                                  |

Also: [dino vs libc](libc.md), when the C library is already portable,
[gotchas](gotchas.md), the platform differences the API can't hide, and
[upgrading](upgrading.md), what changed between SDK versions.

`dino/version.h` has the SDK's version as the numbers `DINO_VERSION_MAJOR`, `DINO_VERSION_MINOR`
and `DINO_VERSION_PATCH`, and the string `DINO_VERSION` (for example `"0.6.0"`, with a suffix such as
`-dev` between releases).

## Conventions

These hold across every module, so each page only mentions exceptions.

**Handles.** Sockets, threads, mutexes, pipes, processes and event loops are opaque handles
(`dino_socket_t`, `dino_thread_t`, ...), integers wide enough for a pointer. Each type has an
invalid value, `DINO_INVALID_SOCKET`, `DINO_INVALID_THREAD` and so on, which functions return on
failure and which you can use to mean "none". Compare against the named constant rather than 0:
`DINO_INVALID_SOCKET` is all bits set, not zero.

**Names.** Everything the SDK defines starts with `dino_` (functions and types) or `DINO_`
(macros and constants), followed by the module: `dino_socket_*`, `dino_event_*`, `dino_process_*`.
Names with a double underscore (`dino__*`, `DINO__*`) are internal.

**Creating handles.** Functions that only construct something return the new handle, or its invalid
value on failure: `dino_socket_create`, `dino_mutex_create`, `dino_event_loop_create`. Functions that
start something or produce more than one result take out-parameters and return `0` or `-1`:
`dino_thread_create`, `dino_process_spawn`, `dino_pipe_create`, `dino_file_size`.

**Return values.** Functions that do something return `0` on success and `-1` on failure. Functions
that wait with a timeout return `DINO_TIMEOUT` (`1`) when the time runs out. Counts (bytes read,
events ready) are non-negative, with `-1` for an error. Failures don't come with a portable reason
code; each header documents what can fail.

**Timeouts** are in milliseconds: `-1` waits forever, `0` checks and returns at once.

**Ownership.** Whatever creates a handle must release it exactly once: close sockets and pipes,
join or detach threads, wait for processes, destroy mutexes and loops. Functions that allocate
for you (`dino_process_run`) have a matching free.

**Threads.** Unless a page says otherwise, functions are safe to call from any thread on different
handles. Using one handle from several threads at once is only safe where the header says so (the
event loop, for example, is designed for it).

**Strings** are UTF-8 on every platform, including paths, arguments and environment variables
passed to the process functions.

**Setup** is automatic. Nothing needs to be called before using any module; see [runtime](runtime.md).

**C++.** Every header can be included from C++ as well as C: the declarations have C linkage.

## Reading the headers

Each page here explains a module with an example. The headers in
[`sdk/include/dino/`](../../sdk/include/dino) are the reference: every function has a doc comment
with its parameters, return values and platform notes. Your editor shows them on hover once
[clangd is set up](../guide/editors.md).
