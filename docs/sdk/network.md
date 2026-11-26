# Network

`#include <dino/core/network.h>` · `dino_core`

Sockets are the **POSIX calls on every target**. `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>` and `<poll.h>` work on Windows too: MinGW ships none of them, so the SDK provides them over Winsock. `bind`, `listen`, `recv`, `sendto`, `setsockopt`, `getsockname`, `shutdown`, `getaddrinfo`, `inet_pton` and `poll` take the same arguments and constants everywhere.

The SDK adds `dino_*` calls only where the platforms differ in a way the standard name can't hide:

- **A Windows socket is a `SOCKET`, not a file descriptor.** `close()` can't close one, so sockets are created and closed with `dino_socket_create` and `dino_socket_close`, and held in a `dino_socket_t`: an `int` on Linux, a `SOCKET` on Windows, so it goes to the POSIX calls without a cast.
- **`send()` raises `SIGPIPE` on Linux** when the peer has gone, which kills the process by default. `dino_socket_send` fails with `DINO_EPIPE` instead, on every platform.
- **Inheritance, would-block errors and datagrams differ.** `dino_socket_accept` makes sockets no child process inherits and keeps them blocking, `dino_socket_connect` reports a connect in progress as `DINO_EINPROGRESS` everywhere, and `dino_socket_recvfrom` cuts an oversized datagram to fit, as Linux does, where Windows fails the call.

Winsock needs `WSAStartup()` first. The SDK starts it before `main()` in any program that includes a socket header, so `getaddrinfo` works before the first socket.

## Example: an echo over loopback

A server and a client in one program. `connect()` completes against the listen backlog before `accept()`, so this needs no threads:

```c
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/network.h>

int main(void)
{
    struct addrinfo hints, *addr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("127.0.0.1", "19101", &hints, &addr) != 0)
        return 1;

    dino_socket_t server = dino_socket_create(addr->ai_family, addr->ai_socktype, 0);
    if (server == DINO_INVALID_SOCKET)
        return 1;
    dino_socket_set_reuseaddr(server, 1);
    if (bind(server, addr->ai_addr, addr->ai_addrlen) != 0 || listen(server, 4) != 0)
        return 1;

    dino_socket_t client = dino_socket_create(addr->ai_family, addr->ai_socktype, 0);
    if (client == DINO_INVALID_SOCKET
        || dino_socket_connect(client, addr->ai_addr, addr->ai_addrlen) != 0)
        return 1;
    dino_socket_t conn = dino_socket_accept(server, NULL, NULL);
    if (conn == DINO_INVALID_SOCKET)
        return 1;
    freeaddrinfo(addr);

    const char *msg     = "ping";
    char        buf[16] = { 0 };
    dino_socket_send(client, msg, strlen(msg), 0);
    int n = (int)recv(conn, buf, sizeof(buf) - 1, 0); // one small message arrives whole on loopback
    dino_socket_send(conn, buf, (size_t)n, 0);
    memset(buf, 0, sizeof(buf));
    n = (int)recv(client, buf, sizeof(buf) - 1, 0);
    printf("echoed %d bytes: %s\n", n, buf);

    dino_socket_close(conn);
    dino_socket_close(client);
    dino_socket_close(server);
    return strcmp(buf, msg) == 0 ? 0 : 1;
}
```

## Functions

| Function                                                     | Does                                                                                                                                                                                                                                             | Returns                                                        |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| `dino_socket_create(domain, type, protocol)`                 | `socket(2)`: `AF_INET` or `AF_INET6`, `SOCK_STREAM` or `SOCK_DGRAM`, protocol `0` or `IPPROTO_*`. Never inherited by child processes; an `AF_INET6` socket starts v6-only on every platform.                                                     | the socket, or `DINO_INVALID_SOCKET`                           |
| `dino_socket_close(sock)`                                    | `close(2)` on Linux, `closesocket` on Windows.                                                                                                                                                                                                   | `0`, `-1`                                                      |
| `dino_socket_accept(sock, addr, len)`                        | `accept(2)`, with the peer's address in `addr` (may be `NULL`). The new socket is blocking and not inherited, and a signal doesn't interrupt the wait.                                                                                           | the new socket, or `DINO_INVALID_SOCKET`                       |
| `dino_socket_connect(sock, addr, len)`                       | `connect(2)`. In progress (non-blocking, or interrupted) is `-1` with `DINO_EINPROGRESS` on every platform.                                                                                                                                      | `0`, `-1`                                                      |
| `dino_socket_connect_timeout(sock, addr, len, timeout_ms)`   | Connect, giving up after `timeout_ms` (`-1`: the system's own limit, about 21 s on Windows and two minutes or more on Linux). The socket is blocking afterwards. Since 1.0.                                                                      | `0`, `DINO_TIMEOUT`, `-1`                                      |
| `dino_socket_set_timeouts(sock, recv_ms, send_ms)`           | Limit how long a blocking receive or send waits (`SO_RCVTIMEO`/`SO_SNDTIMEO`, a `timeval` on POSIX and a `DWORD` on Windows); `0` is no limit, `-1` leaves one as it is. A receive or send that runs out fails with `DINO_ETIMEDOUT`. Since 1.0. | `0`, `-1`                                                      |
| `dino_socket_send(sock, buf, len, flags)`                    | `send(2)`, never raising `SIGPIPE`.                                                                                                                                                                                                              | bytes sent, `-1` on error                                      |
| `dino_socket_recvfrom(sock, buf, len, flags, from, fromlen)` | `recvfrom(2)` as on Linux: an oversized datagram is cut to fit; `from` may be `NULL`.                                                                                                                                                            | bytes received, `0` when a stream's peer closed, `-1` on error |
| `dino_socket_set_reuseaddr(sock, enable)`                    | So a server can rebind its port right after restarting (`SO_REUSEADDR` on Linux; Windows allows it already, and its `SO_REUSEADDR` would let another socket take the port, so it isn't set).                                                     | `0`, `-1`                                                      |
| `dino_socket_set_nonblocking(sock, enable)`                  | `fcntl(O_NONBLOCK)` on Linux, `ioctlsocket(FIONBIO)` on Windows, which has no `fcntl`.                                                                                                                                                           | `0`, `-1`                                                      |
| `dino_socket_error(sock)`                                    | The error waiting on the socket (`SO_ERROR`) as a `DINO_E*` code: how a non-blocking connect ended.                                                                                                                                              | `0`, or a `DINO_E*` code                                       |
| `dino_socket_last_error()`                                   | Why the last POSIX socket call on this thread failed, as a `DINO_E*` code: `errno` on Linux, `WSAGetLastError()` on Windows.                                                                                                                     | a `DINO_E*` code                                               |
| `dino_net_init()`                                            | Start the socket subsystem now, to fail early with a clear message. Optional.                                                                                                                                                                    | `0`, `-1`                                                      |

The `dino_socket_*` calls report why they failed through [`dino_error()`](error.md). The POSIX calls report through `errno` on Linux and `WSAGetLastError()` on Windows; `dino_socket_last_error()` reads whichever applies.

## Writing portable socket code

- **Buffers and option values** are `char *` on Windows. Cast option values: `setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&on, sizeof(on))` builds on every target; a `void *` buffer converts on its own.
- **Return types:** `recv`, `send` and `sendto` return `ssize_t` on Linux and `int` on Windows. Store the result in an `int`.
- **Include order:** the socket headers include Winsock 2. On Windows, include them before `<windows.h>`, or define `WIN32_LEAN_AND_MEAN` first: `<windows.h>` alone brings in Winsock 1, which conflicts.
- **IPv4 and IPv6 are separate.** An IPv6 socket is v6-only on every platform (Linux and Windows default differently). For one dual-stack listener on `"::"`, clear `IPV6_V6ONLY` with `setsockopt` before `bind`.
- **Datagrams:** receive them with `dino_socket_recvfrom`. A plain `recv` of a datagram larger than the buffer fails on Windows. A client that went away doesn't break a UDP server: Windows would report the ICMP "port unreachable" from the next receive, and `dino_socket_create` turns that off.
- **`poll` and `select`** work on sockets everywhere; on Windows they don't take pipes or files (see [poll and select](io.md)).

## Pitfalls

- **Closing a socket doesn't stop a thread blocked on it.** A thread in an accept or receive wakes when another thread closes the socket on Windows, not on Linux (where `shutdown` wakes it, and the descriptor may meanwhile be reused). To be able to stop such a thread, wait with `poll`, a receive timeout or the [event loop](event.md) instead of blocking.
- **Short transfers.** `send` may send fewer bytes than asked, and `recv` returns whatever has arrived: a message can come in pieces, or several can come together. Loop until you have what your protocol says you should.
- **Non-blocking sockets** fail when they would have waited: the `dino_socket_*` calls with `DINO_EAGAIN` in `dino_error()`, the POSIX ones with `DINO_EAGAIN` from `dino_socket_last_error()`. Use `poll` or the [event loop](event.md) to learn when to call again.
- **After a timeout, close the socket.** A `dino_socket_connect_timeout` that ran out may still be connecting in the background, and Windows leaves a socket whose receive or send timed out in an undefined state, with data possibly lost or partly sent. Only `dino_socket_recvfrom` and `dino_socket_send` report a timeout as `DINO_ETIMEDOUT` everywhere: a raw `recv` says `EAGAIN` on Linux and `WSAETIMEDOUT` on Windows.
- **`dino_socket_connect_timeout` leaves the socket blocking,** whatever it was: Windows can't say which mode a socket is in, so it can't be put back. For a non-blocking client, use `dino_socket_connect` and wait as below.
- **A non-blocking `connect`** returns `-1` with `DINO_EINPROGRESS` once it has started. Wait for the socket to become writable, then call `dino_socket_error(sock)`: `0` means connected, anything else is why not. [Errors](error.md) has the whole sequence as a program.
- **Closing.** Close every socket exactly once, with `dino_socket_close`. A socket registered with an event loop must be removed from it (`dino_event_del_socket`) before it's closed.

See also: [`examples/network_example`](../../examples/network_example/main.c), [`examples/ipv6_example`](../../examples/ipv6_example/main.c), and the header for every parameter.
