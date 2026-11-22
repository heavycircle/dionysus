# Platform gotchas

Differences between Linux and Windows that the SDK can't hide, because they're in C itself, in the
C library, or in how the systems work. Code that runs on one platform and fails on the other usually
trips over one of these.

## Printing 64-bit integers

`long` is 64 bits on 64-bit Linux and 32 bits on Windows, and `%ld` follows it. For `uint64_t` and
`int64_t` (which the SDK returns for times, sizes and IDs), use the `inttypes.h` macros, or cast to
`unsigned long long` and use `%llu`:

```c
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint64_t big = 5000000000u;
    size_t   n   = sizeof(big);
    char     a[32], b[32], c[32];
    snprintf(a, sizeof(a), "%" PRIu64, big);
    snprintf(b, sizeof(b), "%llu", (unsigned long long)big);
    snprintf(c, sizeof(c), "%zu", n);
    printf("%s %s %s\n", a, b, c);
    return strcmp(a, "5000000000") == 0 && strcmp(b, a) == 0 && strcmp(c, "8") == 0 ? 0 : 1;
}
```

`size_t` is printed with `%zu` on every target.

## `long` is 32 bits on Windows

The same applies beyond `printf`. Values that need 64 bits, such as file offsets, sizes and
timestamps in ns, belong in `int64_t`/`uint64_t`, not `long`. `ftell` and `fseek` return and take a
`long`, so they fail past 2 GB on Windows. Get sizes with [`dino_file_size`](fs.md).

## Sockets aren't file descriptors

On Linux a socket is a small integer like any file, so `read`, `write` and `close` work on it. On
Windows a socket is a `SOCKET` handle, and those calls don't. Use `dino_socket_recv`, `_send` and
`_close`, and keep sockets in `dino_socket_t` variables, not `int`. The invalid value is
`DINO_INVALID_SOCKET` (all bits set), not `-1` or 0.

## Text and binary files

Windows' C runtime translates `\n` to `\r\n` when writing a file opened in text mode, and back when
reading. Open files you don't want changed in binary mode (`"rb"`, `"wb"`), which behaves the same on
Linux.

## Paths

- **Separators:** Windows accepts both `/` and `\` as path separators, so `/` works everywhere in
  paths you build.
- **Case and names:** Windows paths are case-insensitive, and names such as `CON`, `NUL` and
  `COM1` are reserved.
- **No `/dev/null` or `/tmp`:** use `NUL` and `%TEMP%` on Windows.

## Programs started without stdio

A program started by the Linux kernel (a `core_pattern` handler, `call_usermodehelper`) has no
stdin, stdout or stderr, so a `printf` can land in a file it opened. The SDK fixes this before
`main()`; see [files](fs.md#stdio-for-programs-the-kernel-starts).

## `SIGPIPE`

On Linux, writing to a socket or pipe whose reader has gone raises `SIGPIPE`, which kills the
process by default. `dino_socket_send` and `dino_pipe_write` return `-1` instead. Plain `write()` on
a socket or pipe doesn't, which is one more reason to use the `dino_*` calls.

## Exit codes

On Linux only the low 8 bits of an exit code survive (`exit(256)` reads as 0); Windows keeps 32 bits. Stay within 0 to 125 for codes that mean the same everywhere.

Tell a crash from an exit with `status.signal`, not with `status.code`: a signal on Linux, or a crash on Windows, sets `signal`, and `code` then holds each platform's own number (`128 + signal` on Linux, the `NTSTATUS` on Windows, such as `0xC0000005`). Two differences remain:

- **`abort()`** ends a Linux program with `SIGABRT`, but a MinGW program with exit code 3 and no signal.
- **A Windows program that exits with a code that looks like an error `NTSTATUS`** (both top bits set) is reported as a crash, since Windows itself can't tell the two apart.

## Stack size

Threads started with `dino_thread_create` get 8 MB of stack everywhere. The main thread doesn't: it has 8 MB on Linux and 16 MB on Windows, so a large local array in `main` that works on Windows can overflow on Linux. Allocate big buffers with `malloc`.

## Line endings in output

Under wine, and on Windows, output a program writes to a console or pipe in text mode gets `\r\n`.
Tests comparing a child's output should strip `\r` or compare line by line.

## Endianness

The `ppc` target is big-endian; the others are little-endian. Code that writes integers to files or
the network byte by byte must pick an order explicitly rather than copying memory. `ppc` is there to
catch exactly this.
