# dino vs libc

Write plain C. Every target links a real C library (musl on Linux, MinGW-w64's runtime over the UCRT on Windows), and with the flags dino sets, most of C and much of POSIX already behaves the same on all of them. The SDK adds a `dino_*` call only for one of three reasons:

1. **The platforms differ in a way the standard name can't hide.** A Windows socket isn't a file descriptor, so no `close()` can work on both.
2. **There is no standard form.** Waiting on sockets and pipes together has no portable call at all.
3. **It is measurably smaller or faster.** A `dino_*` call that is neither, and has no semantic reason, is a bug in the SDK.

Where a standard name exists but Windows gets it wrong, the SDK prefers to fix Windows behind that name over adding a new one. It already does this for UTF-8 in `fopen`, `getenv` and `argv`.

## Use the C library

| Area            | Use                                                                         | Notes                                                                                                                                                                         |
| --------------- | --------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Memory          | `malloc`, `calloc`, `realloc`, `free`, `memcpy`, ...                        | `aligned_alloc` is missing on Windows (see [gaps](#gaps))                                                                                                                     |
| Strings         | `string.h`, `ctype.h`, `snprintf`, `strtol`, `strtok_r`, `strndup`, ...     | `memmem` and `strsep` are missing on Windows                                                                                                                                  |
| Formatted I/O   | `printf`, `fprintf`, `sscanf`, ...                                          | 64-bit integers: see [gotchas](gotchas.md#printing-64-bit-integers)                                                                                                           |
| Files           | `fopen`, `fread`, `fwrite`, `fgets`, `fclose`, `stat`, `S_ISDIR`, `S_ISREG` | binary files need `"rb"`/`"wb"` (see gotchas); UTF-8 paths on Windows too ([fs](fs.md#utf-8-on-windows)); no `lstat` or symbolic links on Windows                             |
| Large files     | `fseeko`, `ftello`, `ftruncate`, `off_t`, `st_size`                         | 64-bit on every target from 0.9 (Windows builds with `_FILE_OFFSET_BITS=64`). `fseek`/`ftell` take a `long`, which is 32 bits on Windows                                      |
| Directories     | `opendir`, `readdir`, `closedir`, `rmdir`, `getcwd`, `chdir`                | `dirent.h` exists on Windows too, but without `d_type`: `stat` each entry. `mkdir` takes one argument on Windows and two on Linux                                             |
| Temporary files | `tmpfile`, `mkstemp`, `mkdtemp`                                             | the temporary directory itself has no portable name yet                                                                                                                       |
| Paths           | `basename`, `dirname` (`libgen.h`)                                          | each handles its platform's paths, drive letters included; both may modify their argument. Build paths with `/`, which Windows accepts                                        |
| Math            | `math.h`                                                                    | no `-lm` needed                                                                                                                                                               |
| Time of day     | `time`, `timespec_get`, `gmtime_r`, `localtime_r`, `strftime`, `mktime`     | `gmtime` and `localtime` aren't thread-safe; use the `_r` forms, declared on Windows from 0.9. `timegm` is missing on Windows                                                 |
| Elapsed time    | `clock_gettime(CLOCK_MONOTONIC)`                                            | works everywhere; on Windows it pulls in winpthreads, about 25 KB more than [`dino_time_monotonic_ns`](time.md)                                                               |
| Threads         | `pthread_*`, `pthread_rwlock_*`, `sem_*`, `_Thread_local`                   | winpthreads on Windows, linked statically (about 8–10 KB). musl's default thread stack is 128 KiB, Windows' 1 MiB: set it with `pthread_attr_setstacksize` for deep recursion |
| Atomics         | `stdatomic.h`                                                               | C11 atomics work on every target                                                                                                                                              |
| This process    | `getpid`, `exit`, `atexit`                                                  |                                                                                                                                                                               |
| Environment     | `getenv`                                                                    | UTF-8 on Windows 10 1903 and later, through the manifest dino links; to change it, use [`dino_env_*`](process.md#this-process-and-the-environment)                            |
| Ctrl-C          | `signal(SIGINT, handler)` setting a `volatile sig_atomic_t`                 | on Windows the handler runs on another thread, and `SIGTERM` is never sent                                                                                                    |
| Terminals       | `isatty(fileno(f))`                                                         |                                                                                                                                                                               |
| Byte order      | `__builtin_bswap16/32/64`, `__BYTE_ORDER__`                                 | every target compiles with clang through zig; ppc is the one big-endian target                                                                                                |

## Use `dino_*` for these

| Area                         | Instead of                                                                     | Use                                                                   | Why                                                                                                                                                                               |
| ---------------------------- | ------------------------------------------------------------------------------ | --------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Creating and closing sockets | `socket`, `accept`, `connect`, `send`, `recvfrom`, `closesocket`, `WSAStartup` | [network](network.md)                                                 | a Windows socket is a `SOCKET`, not a file descriptor, and needs Winsock started; `send` raises `SIGPIPE` on Linux; inheritance, would-block and oversized-datagram errors differ |
| Waiting on sockets           | `poll`, `select`, `WSAPoll`                                                    | [poll and select](io.md)                                              | `select` ignores its first argument on Windows, and `WSAPoll` can't wait on anything but sockets                                                                                  |
| Event loops                  | epoll, IOCP                                                                    | [event loop](event.md)                                                | no portable form                                                                                                                                                                  |
| Pipes                        | `pipe`, `CreatePipe`                                                           | [pipes](pipe.md)                                                      | Windows' anonymous pipes can't be waited on alongside sockets or used for overlapped I/O                                                                                          |
| Processes                    | `fork`, `exec*`, `posix_spawn`, `waitpid`, `kill`, `CreateProcess`             | [processes](process.md)                                               | Windows has no `fork` or `exec`; a crash is a signal on Linux and an `NTSTATUS` exit code on Windows                                                                              |
| Short sleeps                 | `usleep`, `nanosleep`, `Sleep`                                                 | [time](time.md)                                                       | MinGW's `usleep` and `nanosleep` round down to whole milliseconds, and `Sleep` rounds up to the 15.6 ms timer tick                                                                |
| Monotonic clock              | `clock_gettime`, `QueryPerformanceCounter`                                     | [time](time.md)                                                       | size only: `clock_gettime` works, but costs about 25 KB more on Windows                                                                                                           |
| Timed condition waits        | `pthread_cond_timedwait` with `pthread_condattr_setclock`                      | [`dino_cond_timedwait`](mutex.md)                                     | Windows refuses `CLOCK_MONOTONIC` for condition variables, so a wall-clock change moves the deadline                                                                              |
| Threads and mutexes          | `pthread_create`, `pthread_mutex_*`, `CreateThread`                            | [threads](thread.md), [mutexes](mutex.md)                             | size on Windows only (8–10 KB smaller); larger and slower on Linux. Plain pthreads work everywhere                                                                                |
| Renaming, removing           | `rename`, `remove`                                                             | [`dino_file_rename`, `dino_file_remove`](fs.md#renaming-and-removing) | Windows' `rename` won't replace an existing file, and `remove` won't remove a read-only one                                                                                       |
| Changing the environment     | `setenv`, `unsetenv`, `_putenv`                                                | [`dino_env_*`](process.md#this-process-and-the-environment)           | MinGW has no `setenv`; `_putenv` treats an empty value as unset                                                                                                                   |
| Errors                       | `errno`, `strerror`, `WSAGetLastError`                                         | [errors](error.md)                                                    | Windows reports socket errors outside `errno`, and its `strerror` doesn't know them                                                                                               |

The build warns at the calls in the second column that misbehave somewhere, naming the replacement (see [portability warnings](../guide/portability-warnings.md)). Calls that behave the same everywhere aren't flagged, even where a `dino_*` call exists.

## Gaps

What MinGW lacks that musl has, and what to write instead:

| Missing on Windows             | Instead                                                                            |
| ------------------------------ | ---------------------------------------------------------------------------------- |
| `aligned_alloc`                | `_aligned_malloc`, freed with `_aligned_free` (not `free`)                         |
| `memmem`, `strsep`             | a short loop; `strtok_r` where empty fields don't matter                           |
| `getline`, `getdelim`          | `fgets` in a loop                                                                  |
| `fsync`                        | `_commit(fileno(f))` after `fflush(f)`                                             |
| `timegm`                       | `_mkgmtime`                                                                        |
| `strptime`                     | `sscanf` into a `struct tm`                                                        |
| `realpath`                     | `_fullpath`, which neither resolves symbolic links nor checks that the path exists |
| `strerror_r`                   | `strerror_s`, with its arguments in a different order                              |
| `mkdir(path, mode)`            | `mkdir(path)`                                                                      |
| `sysconf`, `getpagesize`       | `GetSystemInfo`                                                                    |
| `gethostname` without Winsock  | `gethostname` from `<winsock2.h>`, after Winsock is started                        |
| `lstat`, `symlink`, `readlink` | none: symbolic links need Developer Mode or a privilege on Windows                 |
| `flock`, `fcntl` locks         | `LockFileEx`                                                                       |
| `mmap` (`sys/mman.h`)          | `CreateFileMapping` and `MapViewOfFile`                                            |
| `termios`, `ioctl(TIOCGWINSZ)` | the console API (`SetConsoleMode`, `GetConsoleScreenBufferInfo`)                   |

Put per-platform code like this in separate files selected in CMake, not `#ifdef` blocks in shared code (see [platform-specific code](../guide/projects.md#platform-specific-code)).

## Not covered yet

Planned, with no `dino_*` call today: memory-mapped files, file locking, local (`AF_UNIX`) sockets, notification of console close and `SIGTERM`, the temporary and home directories, and the path of the running executable.

## Out of scope

The SDK won't cover these; use a library or platform code:

- **TLS** and other cryptography.
- **Windows services and Unix daemons.**
- **Dynamic loading** (`dlopen`, `LoadLibrary`): every target links statically.
- **Thread pools** and **CPU affinity**.
- **UTF-16 conversion helpers**: only platform code needs them, and it can call `MultiByteToWideChar`.
- **Reading a password**, **parsing dates** (`strptime`), **symbolic links**, **raising the open-file limit** and **the user's name** (`getenv("USER")` on Linux, `getenv("USERNAME")` on Windows).
