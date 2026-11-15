# Examples

One small, standalone CMake project per SDK feature. Each shows the `dino_*` call to use where
Linux (musl) and Windows (MinGW-w64) differ, and builds unchanged for every target.

```sh
dino run -C examples/network_example          # build for the host and run
dino build --all -C examples/network_example  # build for every target
dino run arm64 -C examples/io_example         # run a foreign target under qemu/wine
```

Every example runs without arguments. `tests/examples.sh`, and so CI, builds each one for all seven
targets and runs it that way; the socket examples check every step and exit nonzero on failure.

| **Category** | **Location**                                  | **SDK header**          | **Description**                                                                                                                     |
| ------------ | --------------------------------------------- | ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Big Files    | [`fs_example`](./fs_example/main.c)           | `dino/core/fs.h`        | Prints the size of `bigfile.img` with `dino_file_size`, which handles files over 2 GB on Windows. Prints 0 if the file is missing.   |
| Heap         | [`heap_example`](./heap_example/main.c)       | none                    | Plain `malloc`/`calloc`/`realloc`/`free` and float formatting. Shows the C library's allocator works on every target with no wrapper. |
| Poll         | [`io_example`](./io_example/main.c)           | `dino/core/io.h`        | Ping/pong where every receive waits on `dino_poll`; also checks timeouts, writability and a peer hang-up.                             |
| Networking   | [`ipv6_example`](./ipv6_example/main.c)       | `dino/core/network.h`   | Echo over `127.0.0.1` and `::1`; invalid addresses such as `localhost` are rejected.                                                |
| Mutexes      | [`mutex_example`](./mutex_example/main.c)     | `dino/core/mutex.h`     | Eight threads increment a shared counter under `dino_mutex_lock`, then compare it with the expected total.                          |
| Networking   | [`network_example`](./network_example/main.c) | `dino/core/network.h`   | TCP ping/pong with `dino_socket_*`.                                                                                                 |
| Runtime      | [`runtime_example`](./runtime_example/main.c) | `dino/core/runtime.h`   | Same ping/pong with `DINO_ENABLE_AUTO_RUNTIME`, which runs `dino_net_init` and friends before `main` and cleans up after it.         |
| Sleep        | [`sleep_example`](./sleep_example/main.c)     | none yet                | Sleeps 3 seconds with plain `sleep(3)`. Switches to the SDK's sleep call once the time module (#17) exists.                         |
| Threading    | [`thread_example`](./thread_example/main.c)   | `dino/core/thread.h`    | Four `dino_thread_create` workers counting to a billion, then `dino_thread_join`. Built but not run in CI: too slow under emulation. |

`network_example`, `io_example` and `runtime_example` also run as two processes for a
two-terminal demo: start one with `server`, then the other with `client`.

```sh
dino run -C examples/network_example -- server   # terminal 1
dino run -C examples/network_example -- client   # terminal 2
```
