# Upgrading

What changed in the SDK between versions, and how to update code that uses it. The compiler finds
almost everything here: a removed or renamed name fails to build, and the error points at the line.

## To 0.6

0.6 is the last release before 1.0 allowed to break the API. The API review
([#102](https://github.com/heavycircle/dionysus/issues/102)) made names and return values
consistent across the SDK, so 1.0 starts clean.

### Renamed types

Every name in `process.h` now starts with `dino_process_`, and every type in `network.h` with
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

### New

- **C++:** every header can be included from C++ (the declarations have C linkage).
- **UTF-8 paths:** `dino_file_size` takes UTF-8 paths on Windows, like the rest of the API.
- **Portability warnings:** calls to platform functions that have a `dino_*` replacement now warn.
  See [portability warnings](../guide/portability-warnings.md).
- **Organization libraries:** `DINO_LIBRARY_PATH` makes your own libraries available to every
  project. See [organization libraries](../guide/libraries.md).
