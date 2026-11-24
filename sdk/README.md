# Dino SDK

The `dino_*` portability layer: one C API over the places where Linux (musl) and Windows
(MinGW-w64) differ. See [CONTRIBUTING.md](../CONTRIBUTING.md#working-on-the-sdk) for the design
rules and how to add a module.

Code that links the SDK gets a warning when it calls one of the platform APIs below that has a
`dino_*` replacement; see [docs/guide/portability-warnings.md](../docs/guide/portability-warnings.md).

## API Changes

Where Linux and Windows differ, and what the SDK provides for each. Memory maps have no module yet
([#22](https://github.com/heavycircle/dionysus/issues/22)); see [dino vs libc](../docs/sdk/libc.md) for what the C library already does on every target.

| **Domain**   | **Linux**             | **Windows**               | **Required?** | **Description**                                                              | **SDK**                               |
| ------------ | --------------------- | ------------------------- | ------------- | ---------------------------------------------------------------------------- | ------------------------------------- |
| Networking   | `sys/socket.h`        | `winsock2.h`              | Required      | `WSAStartup`/`WSACleanup`                                                    | [`network.h`](../docs/sdk/network.md) |
| Processes    | `fork` / `exec`       | `CreateProcess`           | Required      | Windows has no `fork`                                                        | [`process.h`](../docs/sdk/process.md) |
| Multiplexing | `epoll`               | `IOCP`                    | Required      | Windows has no `epoll`                                                       | [`event.h`](../docs/sdk/event.md)     |
| Threading    | `pthread_create`      | `CreateThread`            | Use pthreads  | winpthreads, linked statically (8–10 KB); the SDK adds monotonic timed waits | [`thread.h`](../docs/sdk/thread.md)   |
| Large Files  | `stat` / `ftell`      | `GetFileSizeEx`           | Recommended   | Windows caps standard files at 2GB                                           | [`fs.h`](../docs/sdk/fs.md)           |
| Directories  | `opendir` / `readdir` | `FindFirstFile`           | Use libc      | MinGW ships `dirent.h` (no `d_type`)                                         | [libc](../docs/sdk/libc.md)           |
| Timing       | `time` / `clock`      | `QueryPerformanceCounter` | Recommended   | Clock has no nanosecond precision                                            | [`time.h`](../docs/sdk/time.md)       |
| Memory Map   | `mmap`                | `CreateFileMapping`       | Recommended   | Slower and bulky                                                             | not yet (#22)                         |
| Sleeping     | `sleep` / `usleep`    | `Sleep`                   | Recommended   | `sleep` works in seconds                                                     | [`time.h`](../docs/sdk/time.md)       |
| Multiplexing | `select`              | `select`                  | Optional      | Inefficient, `event.h` scales better                                         | [`io.h`](../docs/sdk/io.md)           |
