# Poll and select

`#include <dino/core/io.h>` · `dino_core`

Wait until one of a few sockets is ready to read or write. `dino_poll` is `poll()` on Linux and
`WSAPoll()` on Windows, with one set of flags. `dino_select` is `select()` with an opaque set type,
because Linux's `fd_set` is a bitmask of descriptors and Windows' is a list of `SOCKET`s.

Both scan every socket on every call, so their cost grows with the number of sockets. Use them
for a handful. For more, or for pipes, processes and timers, use the [event loop](event.md).

## Example: waiting with `dino_poll`

```c
#include <stdio.h>

#include <dino/core/io.h>
#include <dino/core/network.h>

#define PORT 19201

int main(void)
{
    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(server, 1);
    if (server == DINO_INVALID_SOCKET || dino_socket_bind(server, "127.0.0.1", PORT) != 0
        || dino_socket_listen(server, 4) != 0)
        return 1;

    dino_socket_t client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (dino_socket_connect(client, "127.0.0.1", PORT) != 0)
        return 1;
    dino_socket_t conn = dino_socket_accept(server);

    // Nothing has been sent yet, so this times out.
    dino_pollfd_t pfd = { conn, DINO_POLLIN, 0 };
    int           n   = dino_poll(&pfd, 1, 50);
    printf("before sending: %d ready\n", n);

    dino_socket_send(client, "x", 1);
    n = dino_poll(&pfd, 1, 1000);
    printf("after sending: %d ready, readable: %s\n", n, (pfd.revents & DINO_POLLIN) ? "yes" : "no");

    dino_socket_close(conn);
    dino_socket_close(client);
    dino_socket_close(server);
    return n == 1 && (pfd.revents & DINO_POLLIN) ? 0 : 1;
}
```

## `dino_poll`

```
int dino_poll(dino_pollfd_t *fds, int nfds, int timeout_ms);
```

Each `dino_pollfd_t` has `sock`, the `events` you want, and `revents`, filled in with what's ready.

| Flag           | In `events`                                             | In `revents`                                   |
| -------------- | ------------------------------------------------------- | ---------------------------------------------- |
| `DINO_POLLIN`  | wait for data, a connection to accept, or end of stream | readable                                       |
| `DINO_POLLOUT` | wait until a send won't block, or a `connect` finished  | writable                                       |
| `DINO_POLLERR` | not needed                                              | reported whenever it happens                   |
| `DINO_POLLHUP` | not needed                                              | the peer hung up; reported whenever it happens |

It returns the number of sockets with something in `revents`, `0` on timeout (every `revents` is
then `0`), or `-1` on error.

With nothing to watch (`nfds` is `0`) it waits out the timeout and returns `0`, like a sleep. Windows'
`WSAPoll` would fail at once; the SDK waits there too, so a server whose last client left doesn't
spin.

A peer that closes the connection shows up as `DINO_POLLIN` on Linux (and the next `recv` returns
0\) but as `DINO_POLLHUP` on Windows. Treat either as "read now"; `recv` returning 0 is the portable
signal that the stream ended.

## `dino_select`

```
dino_fd_set_t set = dino_fd_set_create();
dino_fd_set_add(set, sock);
int n = dino_select(max_fd, set, DINO_INVALID_FD_SET, DINO_INVALID_FD_SET, 1000);
if (n > 0 && dino_fd_set_has(set, sock)) { ... }
dino_fd_set_destroy(set);
```

| Function                                                                      | Does                                                          |
| ----------------------------------------------------------------------------- | ------------------------------------------------------------- |
| `dino_fd_set_create()`                                                        | a new, empty set, or `DINO_INVALID_FD_SET`                    |
| `dino_fd_set_destroy(set)`                                                    | free it                                                       |
| `dino_fd_set_zero(set)`, `dino_fd_set_add(set, s)`, `dino_fd_set_del(set, s)` | `FD_ZERO`, `FD_SET`, `FD_CLR`                                 |
| `dino_fd_set_has(set, s)`                                                     | `FD_ISSET`: non-zero if `s` is in the set                     |
| `dino_select(max_fd, rfds, wfds, efds, timeout_ms)`                           | wait; returns the number ready, `0` on timeout, `-1` on error |

- **`max_fd`** is the highest socket plus one, as for `select()`. Windows ignores it, but pass it for
  Linux.
- **Unused sets:** pass `DINO_INVALID_FD_SET` for any set you don't need.
- **Nothing to watch:** with no socket in any set it waits out the timeout and returns `0`, on
  Windows too.
- **Refilling:** `dino_select` overwrites the sets with the ready sockets, so refill them before
  every call.
- **Capacity:** a set holds descriptors below 1024 on Linux and at most 64 sockets on Windows.
  `dino_fd_set_add` returns `-1` for a socket that doesn't fit, `0` otherwise.

Prefer `dino_poll`: it has no size limits and doesn't need the sets rebuilt.

## Pipes and other handles

Both functions take sockets only. Wait on a pipe with `dino_pipe_wait`, or on pipes, sockets,
processes and timers together with the [event loop](event.md).

See also: [`examples/io_example`](../../examples/io_example/main.c).
