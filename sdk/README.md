# Dino SDK

The `dino_*` portability layer: one C API over the places where Linux (musl) and Windows
(MinGW-w64) differ. See [CONTRIBUTING.md](../CONTRIBUTING.md#working-on-the-sdk) for the design
rules and how to add a module.

## API Changes

The following table contains the required API to ensure cross-compatibility between Windows and Linux.

| **Domain**   | **Linux**             | **Windows**               | **Required?** | **Description**                        |
| ------------ | --------------------- | ------------------------- | ------------- | -------------------------------------- |
| Networking   | `sys/socket.h`        | `winsock2.h`              | Required      | `WSAStartup`/`WSACleanup`              |
| Processes    | `fork` / `exec`       | `CreateProcess`           | Required      | Windows has no `fork`                  |
| Multiplexing | `epoll`               | `IOCP`                    | Required      | Windows has no `epoll`; `event.h`      |
| Threading    | `pthread_create`      | `CreateThread`            | Recommended   | `winpthreads` is available but bloated |
| Mutexing     | `pthread_mutex_t`     | `CRITICAL_SECTION`        | Recommended   | `winpthreads` is available but bloated |
| Large Files  | `stat` / `ftell`      | `GetFileSizeEx`           | Recommended   | Windows caps standard files at 2GB     |
| Directories  | `opendir` / `readdir` | `FindFirstFile`           | Recommended   | Slower and bulky                       |
| Timing       | `time` / `clock`      | `QueryPerformanceCounter` | Recommended   | Clock has no nanosecond precision      |
| Memory Map   | `mmap`                | `CreateFileMapping`       | Recommended   | Slower and bulky                       |
| Sleeping     | `sleep` / `usleep`    | `Sleep`                   | Recommended   | `sleep` works in seconds               |
| Multiplexing | `select`              | `select`                  | Optional      | Inefficient, `event.h` scales better   |
