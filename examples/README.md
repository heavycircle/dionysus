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
| Application  | [`kv_app`](./kv_app/)                         | network, utest.h        | A key-value store served over TCP, structured like a real project: logic in a library, a thin `main.c`, and unit plus loopback integration tests in `tests/`. See below. |
| Networking   | [`ipv6_example`](./ipv6_example/main.c)       | `dino/core/network.h`   | Echo over `127.0.0.1` and `::1`; invalid addresses such as `localhost` are rejected.                                                |
| Logging      | [`logging_example`](./logging_example/main.c) | `dino/util/logging.h`   | `DINO_LOG_*` lines on stderr, the level picked by `DINO_LOG` or `dino_log_set_level`; debug and trace compile out under `NDEBUG`.   |
| Mutexes      | [`mutex_example`](./mutex_example/main.c)     | `dino/core/mutex.h`     | Eight threads increment a shared counter under `dino_mutex_lock`, then compare it with the expected total.                          |
| Networking   | [`network_example`](./network_example/main.c) | `dino/core/network.h`   | TCP ping/pong with `dino_socket_*`.                                                                                                 |
| Runtime      | [`runtime_example`](./runtime_example/main.c) | `dino/core/runtime.h`   | Setup is automatic; this one opts out (`DINO_NO_AUTO_RUNTIME`) and checks `dino_runtime_init` up front, then shows cleanup isn't final. |
| Sleep        | [`sleep_example`](./sleep_example/main.c)     | `dino/core/time.h`      | Sleeps 3 seconds with `dino_sleep_ms`, measures it with `dino_time_monotonic_ns`, and fails if it woke early.                        |
| Threading    | [`thread_example`](./thread_example/main.c)   | `dino/core/thread.h`    | Four `dino_thread_create` workers counting to a billion, each sum returned through `dino_thread_join`. Built but not run in CI: too slow under emulation. |

`network_example` and `io_example` also run as two processes for a
two-terminal demo: start one with `server`, then the other with `client`.

```sh
dino run -C examples/network_example -- server   # terminal 1
dino run -C examples/network_example -- client   # terminal 2
```

## Testing your own project

[`kv_app`](./kv_app/) shows the layout `dino test` is built for:

```
kv_app/
  CMakeLists.txt   kv library + kv_app program + one dino_test() per tests/test_*.c
  src/             kv.c (store), protocol.c (requests), server.c (socket I/O), main.c
  tests/           test_kv.c, test_protocol.c (units), test_server.c (loopback integration)
```

- **Keep the logic in a library and `main()` thin.** The program and the tests link the same
  `kv` library, so the tests exercise exactly the code that ships.
- **Test pure logic without sockets** (`test_protocol.c` feeds request strings to `kv_execute`), and
  the I/O with a client and server in one process over loopback (`test_server.c`). Both run
  unchanged under qemu and wine.
- **Run them** with `dino test` (host), `dino test arm64 win64`, or `dino test --all`.
- **Leaks fail tests on Linux targets.** If `kv_del` forgot to free a value, `test_kv` fails with
  `dino leak check: 1 allocation(s) still live at exit`. `dino test --memcheck` then shows where the
  memory came from:

  ```
  2 bytes in 1 blocks are definitely lost
     at malloc
     by kv_set (src/kv.c:59)
     by utest_run_kv_delete (tests/test_kv.c:42)
  ```

`dino init` creates the same structure in miniature: `src/`, `tests/test_main.c`, and the
`dino_test()` loop in `CMakeLists.txt`.
