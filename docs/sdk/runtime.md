# Runtime

No header · `dino_core`

**There is nothing to call.** The SDK sets itself up, so a program can use any module from the first
line of `main()`:

- **Before `main()`** (Linux): any of stdin, stdout and stderr that are closed get pointed at
  `/dev/null` (see [`dino_fs_init`](fs.md#stdio-for-programs-the-kernel-starts)). This is linked
  into every program that links `dino_core`. It runs before the program's own constructors and the initializers of C++ globals, so files they open can't become descriptors 0-2 either. Only a constructor you give a priority of 101 or lower (`__attribute__((constructor(101)))`) can run before it.
- **Before `main()`** (Windows): Winsock is started in any program that includes a socket header (`<sys/socket.h>`, `<netdb.h>`, `<poll.h>`, `dino/core/network.h`, ...), so `getaddrinfo` and the other POSIX socket calls work from the first line. Programs that don't touch sockets don't start it.
- **On first use**, subsystems that can fail start and report failure from that call. `dino_socket_create()` runs `dino_net_init()` (`WSAStartup` on Windows) if Winsock hasn't started, and returns `DINO_INVALID_SOCKET` if it fails.
- **At exit**, the operating system releases everything. No cleanup call is needed.

## Failing early

Some programs prefer to fail at startup rather than at the first socket. Each module that can fail to start has an init function, safe to call any number of times from any thread: `dino_net_init()` starts the network (`WSAStartup` on Windows) and returns `-1` if it can't.

```c
#include <stdio.h>

#include <dino/core/network.h>

int main(void)
{
    if (dino_net_init() != 0)
    {
        fprintf(stderr, "network unavailable\n");
        return 1;
    }
    puts("network ok");
    return 0;
}
```

## Opting out of setup before main

`DINO_NO_AUTO_RUNTIME` leaves the before-`main()` step out of the build, for programs that must not
run anything before `main()`. Set it before `project()`:

```cmake
cmake_minimum_required(VERSION 3.20)
set(DINO_NO_AUTO_RUNTIME ON)
project(app C)
```

Such a program calls [`dino_fs_init()`](fs.md#stdio-for-programs-the-kernel-starts) itself if it can be started
without stdio. [`examples/runtime_example`](../../examples/runtime_example) does this. Everything
that starts on first use still does.
