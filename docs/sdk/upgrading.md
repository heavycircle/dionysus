# Upgrading

What changed in the SDK between versions, and how to update code that uses it. The compiler finds
almost everything here: a removed or renamed name fails to build, and the error points at the line.

## To 0.7

0.7 changes the API where it has to change before 1.0 freezes it. The compiler finds the changes to handle types, `argv` and the renamed names; the changes to `dino_mutex_trylock` and to negative lengths it doesn't.

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
