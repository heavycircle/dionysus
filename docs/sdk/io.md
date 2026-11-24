# Poll and select

`#include <poll.h>`, `<sys/select.h>` · `dino_core` (no SDK header)

Wait until one of a few sockets is ready to read or write with plain **`poll()`** or **`select()`**, on every target. MinGW has no `poll()`, so the SDK defines it on Windows over `WSAPoll`, with what POSIX promises and `WSAPoll` doesn't:

- a negative descriptor (`-1`, `INVALID_SOCKET`) is an unused slot, skipped, where `WSAPoll` reports it as an error;
- `POLLERR`, `POLLHUP` and `POLLNVAL` in `events` are ignored, where some `WSAPoll` versions reject them;
- a peer that closed its side is `POLLIN` as well as `POLLHUP`, so a loop that only checks `POLLIN` still calls `recv` and sees the end of the stream;
- nothing to watch waits out the timeout, where `WSAPoll` fails at once;
- `errno` says why it failed.

`select()` is Winsock's own on Windows, which takes the same `fd_set` macros.

Both scan every socket on every call, so their cost grows with the number of sockets. Use them for a handful. For more, or for pipes, processes and timers, use the [event loop](event.md).

## Example: waiting with `poll`

```c
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/network.h>

int main(void)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(19201);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    dino_socket_t server = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_set_reuseaddr(server, 1);
    if (server == DINO_INVALID_SOCKET || bind(server, (struct sockaddr *)&addr, sizeof(addr)) != 0
        || listen(server, 4) != 0)
        return 1;

    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (dino_socket_connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return 1;
    dino_socket_t conn = dino_socket_accept(server, NULL, NULL);

    // Nothing has been sent yet, so this times out.
    struct pollfd pfd = { conn, POLLIN, 0 };
    int           n   = poll(&pfd, 1, 50);
    printf("before sending: %d ready\n", n);

    dino_socket_send(client, "x", 1, 0);
    n = poll(&pfd, 1, 1000);
    printf("after sending: %d ready, readable: %s\n", n, (pfd.revents & POLLIN) ? "yes" : "no");

    dino_socket_close(conn);
    dino_socket_close(client);
    dino_socket_close(server);
    return n == 1 && (pfd.revents & POLLIN) ? 0 : 1;
}
```

## Differences that remain

- **Sockets only on Windows.** `WSAPoll` and Winsock's `select` take sockets, not pipes, files or the console. Wait on a pipe with [`dino_pipe_wait`](pipe.md), or on pipes, processes and sockets together with the [event loop](event.md).
- **A failed non-blocking connect:** wait for the socket to become writable with `poll`, then read the result with [`dino_socket_error`](network.md). With `select`, Windows reports the failure in the exception set only, so watch both the write and exception sets.
- **Signals:** on Linux a signal handler makes `poll` and `select` return `-1` with `EINTR`, even with `SA_RESTART`. Loop on it, recomputing the timeout; Windows has no signals.
- **`select`'s limits:** Linux's `fd_set` holds descriptors below `FD_SETSIZE` (1024) and Windows' holds at most 64 sockets, whatever their values. `poll` has neither limit.
- **Old Windows:** before Windows 10 version 2004, `WSAPoll` didn't report a failed connect at all; use the event loop, or a timeout and `dino_socket_error`, if that matters.
- **Opting out:** `DINO_NO_LIBC_SHIMS` leaves the SDK's Windows `poll()` out, for a project with its own (see [dino vs libc](libc.md)).

See also: [`examples/io_example`](../../examples/io_example/main.c).
