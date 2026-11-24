# Errors

`#include <dino/core/error.h>` · `dino_core`

Every SDK function reports failure through its return value: `-1`, or the `DINO_INVALID_*` value of a handle. `dino_error()` says why, with the same codes on Linux and Windows, where the operating systems use `errno`, `GetLastError()` and `WSAGetLastError()` with three different sets of numbers.

```c
int n = dino_socket_recv(sock, buf, sizeof(buf));
if (n < 0 && dino_error() == DINO_EAGAIN)
    return; // nothing to read yet: not an error on a non-blocking socket
if (n < 0)
    fprintf(stderr, "recv: %s\n", dino_error_message(dino_error()));
```

## Example: a connect that doesn't block

A non-blocking `dino_socket_connect` returns `-1` at once. `DINO_EINPROGRESS` means it has started; when the socket becomes writable, `dino_socket_error` says how it ended. Anything else is the failure itself.

```c
#include <stdio.h>

#include <dino/core/error.h>
#include <dino/core/io.h>
#include <dino/core/network.h>

int main(void)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (s == DINO_INVALID_SOCKET)
    {
        fprintf(stderr, "socket: %s\n", dino_error_message(dino_error()));
        return 1;
    }
    dino_socket_set_nonblocking(s, 1);

    // Nothing listens on port 1, so this connect is refused.
    int code = 0;
    if (dino_socket_connect(s, "127.0.0.1", 1) != 0)
    {
        code = dino_error();
        if (code == DINO_EINPROGRESS)
        {
            dino_pollfd_t pfd = { s, DINO_POLLOUT, 0 };
            if (dino_poll(&pfd, 1, 10000) == 0)
                code = DINO_ETIMEDOUT;
            else
                code = dino_socket_error(s);
        }
    }
    printf("connect: %s\n", code ? dino_error_message(code) : "connected");

    dino_socket_close(s);
    return code == DINO_ECONNREFUSED ? 0 : 1;
}
```

## The functions

| Function                   | Does                                                                                             | Returns                                 |
| -------------------------- | ------------------------------------------------------------------------------------------------ | --------------------------------------- |
| `dino_error()`             | Why the last SDK call in this thread failed.                                                     | a `DINO_E*` code, `0` if nothing failed |
| `dino_error_message(code)` | A short English description, for logs.                                                           | a constant string, never `NULL`         |
| `dino_socket_error(sock)`  | The error waiting on a socket, which is how a non-blocking connect ends ([network](network.md)). | `0` or a `DINO_E*` code                 |

## The codes

| Code                | Means                                                                   | Typical sources                                                         |
| ------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| `DINO_EAGAIN`       | Not now: a non-blocking call would have had to wait.                    | `recv`, `send`, `accept` on a non-blocking socket                       |
| `DINO_EINPROGRESS`  | A non-blocking connect has started.                                     | `dino_socket_connect`                                                   |
| `DINO_EINVAL`       | An argument is invalid: a null pointer, an invalid handle, a bad value. | every module                                                            |
| `DINO_ENOMEM`       | Out of memory.                                                          | anything that creates a handle                                          |
| `DINO_ENOENT`       | No such file, directory, program or variable; not added to this loop.   | `dino_process_spawn`, `dino_file_size`, `dino_event_mod_*` and `_del_*` |
| `DINO_EACCES`       | Permission denied.                                                      | `dino_process_spawn`, `dino_socket_bind`                                |
| `DINO_EEXIST`       | It already exists: already in this loop.                                | `dino_event_add_*`                                                      |
| `DINO_EMFILE`       | Too many open files, sockets or handles.                                | `dino_socket_create`, `dino_socket_accept`, `dino_pipe_create`          |
| `DINO_ECONNREFUSED` | Nothing is listening at the address.                                    | `dino_socket_connect`, `dino_socket_error`                              |
| `DINO_ECONNRESET`   | The peer reset or aborted the connection.                               | `dino_socket_recv`, `dino_socket_send`                                  |
| `DINO_EPIPE`        | The other end is closed: nothing can be written any more.               | `dino_socket_send`, `dino_pipe_write`                                   |
| `DINO_EADDRINUSE`   | The address is already in use.                                          | `dino_socket_bind`                                                      |
| `DINO_ETIMEDOUT`    | The operation itself timed out.                                         | `dino_socket_connect`, `dino_socket_error`                              |
| `DINO_EUNREACH`     | The network or the host can't be reached.                               | `dino_socket_connect`                                                   |
| `DINO_ENOTCONN`     | Not connected.                                                          | `dino_socket_send`, `dino_socket_shutdown`                              |
| `DINO_ENOTSUP`      | This platform, or this kernel, can't do it.                             | `dino_event_add_process` before Linux 5.3                               |
| `DINO_EOTHER`       | A failure with no code of its own.                                      | anything                                                                |

## Pitfalls

- **Read it right after the failure.** Only a failed call sets the code. A call that succeeds leaves the previous one in place, so `dino_error()` after a success tells you about some earlier failure, and another SDK call in between may replace it.
- **One per thread.** A failure in one thread never changes what another thread reads.
- **`DINO_TIMEOUT` is not a failure.** A wait that runs out of time returns `DINO_TIMEOUT` and leaves the code alone.
- **Compare codes, not messages.** The text of `dino_error_message` is for people and may change.
- **Several system errors share a code.** `DINO_EUNREACH` covers an unreachable network and an unreachable host, `DINO_EAGAIN` covers "would block" and an interrupted call. The codes say what a program can do about a failure, not everything the system knew.
- **The system's own error is not kept.** `errno` and `GetLastError()` after an SDK call are whatever the SDK's last system call left there.

Header: [`dino/core/error.h`](../../sdk/include/dino/core/error.h).
