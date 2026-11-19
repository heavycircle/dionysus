# Runtime

`#include <dino/core/runtime.h>` · `dino_core`

**There is nothing to call.** The SDK sets itself up, so a program can use any module from the first
line of `main()`:

- **Before `main()`** (Linux): any of stdin, stdout and stderr that are closed get pointed at
  `/dev/null` (see [`dino_fs_init`](fs.md#stdio-for-programs-the-kernel-starts)). This is linked
  into every program that links `dino_core`.
- **On first use**, subsystems that can fail start and report failure from that call. The first
  `dino_socket_create()` runs `dino_net_init()` (`WSAStartup` on Windows) and returns
  `DINO_INVALID_SOCKET` if it fails.
- **At exit**, the operating system releases everything. No cleanup call is needed.

## Checking up front

Some programs prefer to fail at startup rather than at the first socket. `dino_runtime_init()`
initializes every subsystem now and reports whether all of them worked:

```c
#include <stdio.h>

#include <dino/core/network.h>
#include <dino/core/runtime.h>

int main(void)
{
    if (dino_runtime_init() != 0)
    {
        fprintf(stderr, "startup failed\n");
        return 1;
    }

    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (s == DINO_INVALID_SOCKET)
        return 1;
    dino_socket_close(s);

    dino_runtime_cleanup(); // optional
    puts("runtime ok");
    return 0;
}
```

| Function                          | Does                                                                                                                  |
| --------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| `int dino_runtime_init(void)`     | Initialize every subsystem now. `0` on success, `-1` if any fails. Safe to call any number of times, from any thread. |
| `void dino_runtime_cleanup(void)` | Tear every subsystem down. Optional. A later call that needs a subsystem initializes it again.                        |

Each module's own init function (`dino_net_init()`, `dino_fs_init()`) does the same for that one
module.

## Opting out of setup before main

`DINO_NO_AUTO_RUNTIME` leaves the before-`main()` step out of the build, for programs that must not
run anything before `main()`. Set it before `project()`:

```cmake
cmake_minimum_required(VERSION 3.15)
set(DINO_NO_AUTO_RUNTIME ON)
project(app C)
```

Such a program calls `dino_fs_init()` (or `dino_runtime_init()`) itself if it can be started
without stdio. [`examples/runtime_example`](../../examples/runtime_example) does this. Everything
that starts on first use still does.
