# dino vs libc

The SDK only wraps what differs between Linux and Windows. Everything else comes from the C library
each target links (musl on Linux, MinGW-w64's runtime on Windows), and for most of C that is already
portable. Use the plain C library for these:

| Area          | Use                                                  | Notes                                                                                                                                                         |
| ------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Memory        | `malloc`, `calloc`, `realloc`, `free`, `memcpy`, ... | works everywhere; no wrapper needed                                                                                                                           |
| Strings       | `string.h`, `ctype.h`, `snprintf`, `strtol`, ...     |                                                                                                                                                               |
| Formatted I/O | `printf`, `fprintf`, `sscanf`, ...                   | 64-bit integers: see [gotchas](gotchas.md#printing-64-bit-integers)                                                                                           |
| Files         | `fopen`, `fread`, `fwrite`, `fgets`, `fclose`        | open binary files with `"rb"`/`"wb"` (see gotchas); UTF-8 paths on Windows too ([fs](fs.md#utf-8-on-windows)); sizes over 2 GB with [`dino_file_size`](fs.md) |
| Math          | `math.h`                                             | no `-lm` needed                                                                                                                                               |
| Time of day   | `time`, `gmtime`, `strftime`                         | durations and sleeping: [time](time.md)                                                                                                                       |
| Atomics       | `stdatomic.h`                                        | C11 atomics work on every target                                                                                                                              |
| Environment   | `getenv`                                             | reading works everywhere; to change it, or for UTF-8 on Windows, use [`dino_env_*`](process.md#this-process-and-the-environment)                              |

Use `dino_*` for these, which differ or don't exist on one side:

| Area                | Instead of                                                                             | Use                                                                   |
| ------------------- | -------------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| Sockets             | `socket`, `send`, `recv`, `closesocket`, `WSAStartup`, ...                             | [network](network.md)                                                 |
| Waiting on sockets  | `poll`, `select`, `WSAPoll`, epoll, IOCP                                               | [poll and select](io.md), [event loop](event.md)                      |
| Threads             | `pthread_create`, `CreateThread`, `_beginthreadex`                                     | [threads](thread.md)                                                  |
| Locks               | `pthread_mutex_*`, `pthread_cond_*`, critical sections                                 | [mutexes](mutex.md)                                                   |
| Pipes               | `pipe`, `CreatePipe`                                                                   | [pipes](pipe.md)                                                      |
| Processes           | `fork`, `exec*`, `posix_spawn`, `waitpid`, `CreateProcess`                             | [processes](process.md)                                               |
| Sleeping and clocks | `sleep`, `usleep`, `nanosleep`, `clock_gettime`, `Sleep`, `QueryPerformanceCounter`    | [time](time.md)                                                       |
| Renaming, removing  | `rename`, `remove` (Windows' don't replace an existing file or remove a read-only one) | [`dino_file_rename`, `dino_file_remove`](fs.md#renaming-and-removing) |

The build warns at each call in the second table that has a `dino_*` replacement, naming it (see
[portability warnings](../guide/portability-warnings.md)).

## Not covered yet

Some platform areas have no `dino_*` API yet. For these, write per-platform code (in separate files
selected in CMake, see [platform-specific code](../guide/projects.md#platform-specific-code)):

- directories (`opendir`/`readdir` vs `FindFirstFile`), planned for 1.1
- memory-mapped files (`mmap` vs `CreateFileMapping`), planned for 1.1
- signals and console control events
- dynamic libraries (everything links statically)
- terminals (raw mode, window size, colours)
