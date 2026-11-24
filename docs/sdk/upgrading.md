# Upgrading

What changed in the SDK between versions, and how to update code that uses it. The compiler finds
almost everything here: a removed or renamed name fails to build, and the error points at the line.

## To 0.9

0.9 brings the SDK back to plain C: a `dino_*` call stays only where the platforms can't be made to agree behind the standard name, where there is no standard name, or where it is measurably smaller or faster.

### Removed: calls the C library already covers

| Removed                                        | Use instead                                                                                                                            |
| ---------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `dino_process_current_id()`                    | `getpid()` from `<unistd.h>`, on every target                                                                                          |
| `dino_time_unix_ms()`                          | `timespec_get(&ts, TIME_UTC)` from `<time.h>`: `ts.tv_sec * 1000 + ts.tv_nsec / 1000000`                                               |
| `dino_runtime_init()`, `<dino/core/runtime.h>` | nothing: setup is automatic. To fail early, call `dino_net_init()`; a program built with `DINO_NO_AUTO_RUNTIME` calls `dino_fs_init()` |
| `dino_runtime_cleanup()`                       | nothing, or `dino_net_cleanup()`, which it called (and which 0.8 made return `int`)                                                    |

### `rename`, `remove`, `setenv`, `unsetenv` and `getentropy` work on Windows

The SDK now defines these on Windows under their standard names, behaving as on Linux and setting `errno`: `rename` replaces an existing file, `remove` deletes read-only files and empty directories, and `setenv`, `unsetenv` and `getentropy`, which MinGW lacks, exist. A project with its own Windows definitions sets `DINO_NO_LIBC_SHIMS` before `project()`.

| Removed                         | Use instead                                                 |
| ------------------------------- | ----------------------------------------------------------- |
| `dino_file_rename(from, to)`    | `rename(from, to)`; check `errno` instead of `dino_error()` |
| `dino_file_remove(path)`        | `remove(path)`                                              |
| `dino_env_get(name, buf, size)` | `getenv(name)`, copying the value if you keep it            |
| `dino_env_set(name, value)`     | `setenv(name, value, 1)`                                    |
| `dino_env_unset(name)`          | `unsetenv(name)`                                            |

On Windows, `getenv` can't see a variable set to an empty value, which `dino_env_get` could.

### Windows: 64-bit file offsets and `gmtime_r`

Windows targets now build with `_FILE_OFFSET_BITS=64` and `_POSIX_THREAD_SAFE_FUNCTIONS`, as musl always has. `off_t` and `struct stat`'s `st_size` are 64-bit, so `stat`, `fseeko`, `ftello` and `ftruncate` work past 2 GB, and `gmtime_r`/`localtime_r` are declared. A prebuilt Windows library compiled without the flag that passes `off_t` or `struct stat` across its API no longer matches; rebuild it with dino.

### Fewer portability warnings

Calls that behave the same on every target are no longer flagged: `pthread_*`, `sched_yield`, `getpid`, `sleep`, `gettimeofday`, `clock_gettime`, `bind`, `listen`, `recv`, `sendto`, `setsockopt` and `shutdown`. `pthread_condattr_setclock` is flagged instead, since Windows refuses `CLOCK_MONOTONIC`. Several messages now give the real reason, such as `usleep` and `nanosleep` rounding down to whole milliseconds on Windows.

### Smaller Windows programs that log

The SDK's own sources no longer switch MinGW to its static printf: a Windows program that logs is about 18 KB smaller.

## To 0.8

### UDP: `sendto`, `recvfrom` and socket addresses

New: `dino_socket_sendto`, `dino_socket_recvfrom`, `dino_socket_local_addr` and `dino_socket_peer_addr`, with `dino_socket_addr_t` (see [Network](network.md#udp)). Two Windows behaviors now match Linux: a datagram larger than the buffer is returned cut to fit instead of failing, and an unconnected UDP socket no longer fails a receive with `DINO_ECONNRESET` after sending to a port nobody listens on.

### More portability warnings

Calls with a `dino_*` replacement that weren't flagged now are: `kill`, `getpid`, `wait`, `setenv`, `gettimeofday`, `sched_yield`, `epoll_*`, C11 `<threads.h>`, `sendto`/`recvfrom` and their Windows counterparts (`TerminateProcess`, `GetCurrentThreadId`, `SetEnvironmentVariable`, `ioctlsocket`, `WSAGetLastError`, ...); see the table in [Portability warnings](../guide/portability-warnings.md#whats-flagged). A project built with `-Werror` that uses them sees new errors: switch to the `dino_*` call, or opt out as that page shows.

### Toolchain fixes

- **`find_package`, `find_library`, `find_path` and pkg-config no longer search the host's system directories**, whose libraries and headers are for another C library. A project that found a host package by accident now gets "not found" at configure time instead of errors at build time. Point `CMAKE_PREFIX_PATH` at libraries built for the target.
- **The SDK's sources are always C11**, so a project can choose C89.
- **LTO** works with static libraries, and **`.rc` files** build for Windows targets, both with zig's own tools.
- **SDK options set after `project()`** now get a warning; they never had an effect.

### `dino` keeps `--` settings

- **A build type given with `--`** (`dino build -- -DCMAKE_BUILD_TYPE=RelWithDebInfo`) stays for later `dino build`, `dino run` and `dino test` until `--debug` or `--release`, instead of being reset to Release by the next command. `--` arguments reconfigure only when they differ from the last ones.
- **A moved or copied project** builds on the first try instead of failing once with CMake cache errors.
- **A `build/<target>` configured with plain CMake** is no longer taken over: `dino` stops instead of deleting its cache. Remove the directory, or configure it elsewhere.

### `dino run` writes only the program's output to stdout

The build's progress and CMake's output now go to stderr, so scripts that read `dino run`'s stdout get the program's output alone. A script that filtered the build lines out can stop doing so.

### Processes: environment entries are checked, output can be capped

- **`opts.env`** entries that are empty or have no `=` now fail `dino_process_spawn` and `dino_process_run` with `DINO_EINVAL` on both platforms. Before, Linux passed them on and Windows silently dropped every variable after an empty one.
- **`dino_process_run`** has a `max_output` option and a `truncated` result, and returns `-1` when reading an output fails, where it used to return `0` with the output cut short.
- **A process handle must not be used after a successful wait:** see the pattern in [Processes](process.md).

### Runtime cleanup, condition waits and logging

- **`dino_runtime_cleanup` and `dino_net_cleanup` return `int`** and refuse, returning `-1` with `DINO_EINVAL`, while a socket from `dino_socket_create` or `dino_socket_accept` is still open. On Windows they used to break every such socket. Code that ignores the result still compiles.
- **`dino_cond_wait` and `dino_cond_timedwait`** return `-1` (`DINO_EINVAL`) when the mutex is held more than once, where they used to hang.
- **`DINO_LOG`** accepts `warning`, and reports any value it doesn't know once on stderr.

### Target conventions fixed for 1.0

- **`CMAKE_SYSTEM_PROCESSOR`** is now what native CMake reports: `ppc` (was `powerpc`), `armv7l` (was `arm`), `x86` for win32 (was `i686`) and `AMD64` for win64 (was `x86_64`). A project or CMake module that matched the old values has to match the new ones (see the table in [Projects](../guide/projects.md)); modules written for native builds already do.
- **The `i386` target** builds for the i686 instead of zig's default Pentium 4, so its programs run on any 32-bit x86 PC and no longer need SSE2. Floating point uses the x87 unit instead; `CFLAGS=-mcpu=pentium4` gives back the old code.
- **Triples whose C library can't link statically** (`x86_64-linux-gnu`, macOS, the BSDs) now link dynamically instead of failing CMake's compiler check.
- **`dino test --all`** skips the targets this host can never run instead of refusing to start.

### Windows executables use UTF-8

Executables from `dino_executable()` and `dino_test()` now carry a manifest that makes their Windows code page UTF-8 (Windows 10 1903 and later), and set the console to UTF-8 while they run: `argv`, `fopen`, `getenv` and `printf` now agree with the SDK's UTF-8 strings. A program that relied on `argv` arriving in the ANSI code page sees UTF-8 instead; one that links its own manifest sets `DINO_NO_UTF8_MANIFEST` (see [fs](fs.md#utf-8-on-windows)).

### `dino_file_rename` and `dino_file_remove`

New, for the two calls whose C library versions differ on Windows: `rename` there fails when the destination exists, `remove` on a read-only file (see [fs](fs.md#renaming-and-removing)).

## To 0.7

0.7 changes the API where it has to change before 1.0 freezes it. The compiler finds the changes to handle types, `argv`, process status and the renamed names; the changes to `dino_mutex_trylock`, to negative lengths and to event flags it doesn't.

### Failures have a reason: `dino_error()`

Every failure was `-1` with nothing to tell one from another, so a non-blocking `recv` that had nothing to read looked like a broken connection. [`dino_error()`](error.md) now gives the reason for the last failed call in the thread, as a `DINO_E*` code that is the same on Linux and Windows. Return values are unchanged, so code that only checks for `-1` keeps working.

```c
// Before: -1 for "nothing yet" and for "connection reset" alike.
if (dino_socket_recv(sock, buf, len) < 0)
    drop(conn);

// After
int n = dino_socket_recv(sock, buf, len);
if (n < 0 && dino_error() != DINO_EAGAIN)
    drop(conn);
```

A non-blocking `dino_socket_connect` fails with `DINO_EINPROGRESS` when it has started, and the new `dino_socket_error(sock)` says how it ended once the socket is writable.

### Handle types are distinct

Every handle type was `uintptr_t`, so `dino_socket_close(pipe)` or swapped arguments compiled without a warning and acted on the wrong object. Now `dino_pipe_t`, `dino_process_t`, `dino_thread_t`, `dino_mutex_t`, `dino_cond_t`, `dino_event_loop_t`, `dino_event_timer_t` and `dino_fd_set_t` are each a pointer to a struct of their own, and mixing them up is a compile error. `dino_socket_t` is still an integer holding the OS socket.

What still works: comparing with `DINO_INVALID_*`, initializing a handle with `0`, and passing one through a `void *`. What breaks is keeping a handle in an integer or doing arithmetic on it:

```c
// Before
uintptr_t saved = (uintptr_t)pipe;
dino_pipe_close((dino_pipe_t)saved);

// After: keep it in its own type, or in a void * with a cast back.
dino_pipe_t saved = pipe;
dino_pipe_close(saved);
```

### Bad arguments fail instead of crashing or being guessed at

- **Null out-parameters** to `dino_thread_create`, `dino_pipe_create`, `dino_process_spawn` and `dino_poll` fail with `-1` and `DINO_EINVAL`; they crashed before.
- **Values outside the socket enums** make `dino_socket_create` return `DINO_INVALID_SOCKET`. Before, an unknown domain, type or protocol quietly became IPv4, stream or "any", so passing a platform's own `AF_INET6` by habit gave an IPv4 socket.
- **`dino_pipe_wait` on a write end** fails with `-1` on Linux too; it timed out there, or reported readable once the reader was gone.
- **`dino_env_get` leaves an empty string** in `buf` when it fails, instead of whatever was there.

### Lengths are `size_t`

`dino_socket_send`, `dino_socket_recv`, `dino_pipe_read`, `dino_pipe_write` and `dino_socket_setopt` take their length as `size_t`, like `strlen` and `sizeof`, so the casts can go. A length over 2 GB used to wrap: a 4 GB + 16 byte buffer was sent as 16 bytes. Now one call moves at most `INT_MAX` bytes and returns how many it moved, which callers already handle, since a transfer may always be short. Return values are still `int`.

```c
// Before
dino_socket_send(sock, msg, (int)strlen(msg));

// After
dino_socket_send(sock, msg, strlen(msg));
```

A negative length used to fail with `-1`. Now it converts to a huge `size_t`, as it would for `memcpy`: pass lengths from `size_t` variables.

### `argv` and `env` are `const char *const`

`dino_process_spawn`, `dino_process_run` and the `env` field of both option structs take `const char *const` arrays, so a list of string literals compiles without a warning in C++ and under `-Wwrite-strings`.

```c
// Before: warns in C++, and in C with -Wwrite-strings
char *const argv[] = { "/bin/sh", "-c", "echo hi", NULL };

// After
const char *const argv[] = { "/bin/sh", "-c", "echo hi", NULL };
```

Arrays declared `char *const argv[]` must change: C doesn't convert them to the new type without a warning. To forward `main`'s `argv` in C, cast it: `dino_process_spawn(&p, (const char *const *)argv, NULL, NULL)`. C++ needs no cast.

### Two renames

| Before                 | After                  |
| ---------------------- | ---------------------- |
| `dino_thread_fn`       | `dino_thread_fn_t`     |
| `dino_event_timer_add` | `dino_event_add_timer` |
| `dino_event_timer_del` | `dino_event_del_timer` |

`dino_thread_fn` was the only type without `_t`, and the timer calls now read like `dino_event_add_socket`. The old names are gone; the compiler points at each use.

### Event flags at the end of a stream are the same on every platform

Three differences between Linux and Windows are gone; each changes what one platform reports:

- **A socket at the end of the stream, or reset, reports `READ`** with `HANGUP` (and `ERROR` for a reset) on Windows too, when it is watched for `READ`. Before, Windows reported `HANGUP` alone, so a handler that only looked at `READ` never saw the connection close.
- **A pipe reports `HANGUP` only once it is drained** on Linux too. Before, Linux reported `READ | HANGUP` as soon as the last writer closed, with data still waiting, so a handler that stopped at `HANGUP` lost the tail of the output.
- **A socket watched for neither `READ` nor `WRITE` no longer reports a peer's close or shutdown**, only a reset. Before, a half-closed peer ended every wait at once for as long as the socket stayed in the loop.

### How a process ended: `dino_process_status_t`

The exit code was an `int` that meant different things per platform: a crashed child gave `139` on Linux and a negative `NTSTATUS` on Windows, with no portable way to tell a crash from an exit. `dino_process_wait`, `dino_process_wait_timeout` and the result of `dino_process_run` now report a `dino_process_status_t` with `code` and `signal`. `signal` is `0` for an exit and the matching `<signal.h>` number for a signal or a crash, on both platforms.

```c
// Before
int code;
dino_process_wait(child, &code);
if (code > 128) /* a signal, on Linux only */;
printf("%d\n", result.exit_code);

// After
dino_process_status_t status;
dino_process_wait(child, &status);
if (status.signal) /* a signal or a crash, on both */;
printf("%d\n", result.status.code);
```

`dino_process_kill` now reports `signal` `9` as well as `code` `DINO_EXIT_KILLED` on Windows too.

### `dino_poll`, `dino_select` and sockets behave the same on Linux and Windows

These change what one platform reports, without any change to the code that calls them:

- **A closed peer is `DINO_POLLIN | DINO_POLLHUP`** from `dino_poll` on both platforms. Linux reported `DINO_POLLIN` alone, Windows `DINO_POLLHUP` alone, so a loop that only checked `DINO_POLLIN` spun on Windows.
- **A failed non-blocking connect is in the write set** of `dino_select` on Windows too. Before, it was only in the exception set there, and a caller watching the write set waited out its timeout.
- **A signal no longer ends a wait with `-1`** on Linux: `dino_poll`, `dino_select`, `dino_socket_recv`, `send` and `accept` carry on as on Windows. An interrupted `dino_socket_connect` fails with `DINO_EINPROGRESS` and finishes in the background.
- **`dino_socket_set_reuseaddr` does nothing on a UDP socket**, as on Windows. On Linux it used to let a second socket bind the same port and take its datagrams.

### Linux release builds link for speed

Release builds on the Linux targets now link at `-O2`, so the C library's `memcpy`, `memset` and allocator are built for speed: about 20 times faster for `memcpy`, for under a kilobyte more per program. Windows targets still link for size. A project that wants the old, smallest binaries on Linux sets `DINO_LINK_FOR` to `size` (see [Building](../guide/building.md#what-release-optimizes-for)).

### `dino_mutex_trylock` returns `DINO_TIMEOUT` when the mutex is held

It returned `-1` both for a mutex another thread holds and for an invalid handle. Held elsewhere is now `DINO_TIMEOUT`, the answer of every other wait that would have had to wait, and `-1` is left for errors.

```c
// Before
if (dino_mutex_trylock(m) != 0)
    return; // busy, or broken

// After: the same test still works, since DINO_TIMEOUT is not 0. To tell them apart:
int rc = dino_mutex_trylock(m);
if (rc == DINO_TIMEOUT)
    return; // busy
```

Code that tested `== -1` for "busy" must test for `DINO_TIMEOUT`.

## To 0.6.1

Fixes only; nothing fails to build. These change what a program sees at run time.

### `dino_process_run` keeps to its timeout

A process the child started in the background, holding stdout, stderr or stdin open, kept
`dino_process_run` waiting long past `timeout_ms`. It now stops at the deadline and returns
`DINO_TIMEOUT`, with the output read so far and the child's own `exit_code`, where it used to
return `0` once the background process let go.

### Linux children no longer inherit your descriptors

A descriptor the program opened without close-on-exec (with `open`, `dup` or `socket` rather than
the SDK) reached every child on Linux, though the docs said children get only stdin, stdout and
stderr, as on Windows. It is closed in the child now. A program that handed a child a descriptor
this way passes it as stdin, stdout or stderr instead.

### A relative program path is found from `cwd` on Windows too

`dino_process_spawn` with `cwd` set and a relative `argv[0]` such as `./tool` looked for the
program in the new directory on Linux but in the parent's on Windows. Both use `cwd` now. Code that
relied on Windows' old behaviour passes an absolute path.

## To 0.6

0.6 is the last release before 1.0 allowed to break the API. The API review
([#102](https://github.com/heavycircle/dionysus/issues/102)) made names and return values
consistent across the SDK, so 1.0 starts clean.

### Renamed types

Every type in `process.h` now starts with `dino_process_`, and every type in `network.h` with
`dino_socket_`. Rename them in your code; their contents are unchanged.

| 0.5                      | 0.6                          |
| ------------------------ | ---------------------------- |
| `dino_spawn_opts_t`      | `dino_process_spawn_opts_t`  |
| `dino_run_opts_t`        | `dino_process_run_opts_t`    |
| `dino_run_result_t`      | `dino_process_result_t`      |
| `dino_run_result_free()` | `dino_process_result_free()` |
| `dino_domain_t`          | `dino_socket_domain_t`       |
| `dino_type_t`            | `dino_socket_type_t`         |
| `dino_protocol_t`        | `dino_socket_protocol_t`     |
| `dino_shutdown_how_t`    | `dino_socket_shutdown_t`     |

The constants (`DINO_AF_INET`, `DINO_SOCK_STREAM`, `DINO_IPPROTO_TCP`, `DINO_SHUT_RDWR`, ...) keep
their names. A `sed` that does it all:

```sh
sed -i -E 's/\bdino_spawn_opts_t\b/dino_process_spawn_opts_t/g; s/\bdino_run_opts_t\b/dino_process_run_opts_t/g;
  s/\bdino_run_result_t\b/dino_process_result_t/g; s/\bdino_run_result_free\b/dino_process_result_free/g;
  s/\bdino_domain_t\b/dino_socket_domain_t/g; s/\bdino_type_t\b/dino_socket_type_t/g;
  s/\bdino_protocol_t\b/dino_socket_protocol_t/g; s/\bdino_shutdown_how_t\b/dino_socket_shutdown_t/g' src/*.c
```

### `dino_file_size` reports errors

It returned `0` for a missing file, the same as for an empty one. It now returns `0` or `-1` and
stores the size through a pointer:

```
/* 0.5 */
uint64_t size = dino_file_size("data.bin");

/* 0.6 */
uint64_t size;
if (dino_file_size("data.bin", &size) != 0)
    /* missing, a directory, or unreadable */;
```

### `dino_fd_isset` is `dino_fd_set_has`

The arguments are now in the same order as `dino_fd_set_add` and `dino_fd_set_del`, set first:

```
/* 0.5 */  if (dino_fd_isset(sock, set)) ...
/* 0.6 */  if (dino_fd_set_has(set, sock)) ...
```

### Removed

| Removed                                                       | Instead                                                                           |
| ------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `DINO_ENABLE_AUTO_RUNTIME;`                                   | delete the line: setup has been automatic since 0.3                               |
| `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` | `DINO_LOG_TRACE` ... `DINO_LOG_ERROR`. The short names clashed with `<syslog.h>`. |
| `dino_fs_cleanup()`                                           | delete the call: it did nothing                                                   |
| `#include <dino/core/kernel.h>`                               | delete the line: the header was empty                                             |

### Timeouts are `int`

`dino_cond_timedwait` took a `uint32_t` and `dino_select` a `long`. Both take `int timeout_ms` now,
like every other timeout, and `dino_cond_timedwait(c, m, -1)` waits forever. Existing calls with a
positive timeout compile unchanged.

### Failures return `-1`

`dino_mutex_lock`, `dino_mutex_unlock`, `dino_runtime_init` and `dino_net_init` could return a
platform error code or `1` on failure. They return `-1` now, like everything else. Code that checks
`!= 0` is unaffected. Code that compared the result against a `pthread` error code must check for
`-1` instead.

### IPv6 sockets are v6-only on Linux too

An IPv6 socket bound to `"::"` accepted IPv4 connections on Linux but not on Windows. It is v6-only
on both now. A Linux server that relied on one socket for both families either binds a second,
IPv4 socket to `"0.0.0.0"` on the same port, or calls `dino_socket_set_v6only(sock, 0)` before
`dino_socket_bind`.

### Accepted sockets are blocking on Windows too

`dino_socket_accept` on a non-blocking listener returned a non-blocking socket on Windows and a
blocking one on Linux. Both return a blocking socket now. Code that relied on the Windows behaviour
calls `dino_socket_set_nonblocking(conn, 1)` after accepting, which works on every version.

### New

- **C++:** every header can be included from C++ (the declarations have C linkage).
- **`dino_fd_set_add` returns `int`:** `-1` for a socket that doesn't fit the set (1024 and up on
  Linux, the 65th on Windows), which it used to write out of bounds or drop. Existing calls compile
  unchanged.
- **UTF-8 paths:** `dino_file_size` takes UTF-8 paths on Windows, like the rest of the API.
- **Portability warnings:** calls to platform functions that have a `dino_*` replacement now warn.
  See [portability warnings](../guide/portability-warnings.md).
- **Organization libraries:** `DINO_LIBRARY_PATH` makes your own libraries available to every
  project. See [organization libraries](../guide/libraries.md).
