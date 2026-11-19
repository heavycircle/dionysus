# Network

`#include <dino/core/network.h>` · `dino_core`

TCP and UDP sockets over IPv4 and IPv6, with one API for Berkeley sockets on Linux and Winsock on
Windows. It hides the differences that bite portable code:

- Windows sockets are `SOCKET` handles, not small integers, and are closed with `closesocket()`.
  Here they're `dino_socket_t` and close with `dino_socket_close()`.
- Winsock needs `WSAStartup()` first. `dino_socket_create()` calls it for you.
- `send()` on a socket whose peer has gone raises `SIGPIPE` on Linux, which kills the process by
  default. `dino_socket_send()` returns `-1` instead, on every platform.
- Non-blocking mode, socket options and shutdown use different calls and constants per platform.

## Example: an echo over loopback

A server and a client in one program. `connect()` completes against the listen backlog before
`accept()`, so this needs no threads:

```c
#include <stdio.h>
#include <string.h>

#include <dino/core/network.h>

#define PORT 19101

int main(void)
{
    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (server == DINO_INVALID_SOCKET)
        return 1;
    dino_socket_set_reuseaddr(server, 1);
    if (dino_socket_bind(server, "127.0.0.1", PORT) != 0 || dino_socket_listen(server, 4) != 0)
        return 1;

    dino_socket_t client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (client == DINO_INVALID_SOCKET || dino_socket_connect(client, "127.0.0.1", PORT) != 0)
        return 1;
    dino_socket_t conn = dino_socket_accept(server);
    if (conn == DINO_INVALID_SOCKET)
        return 1;

    const char *msg = "ping";
    char        buf[16] = { 0 };
    dino_socket_send(client, msg, (int)strlen(msg));
    int n = dino_socket_recv(conn, buf, sizeof(buf) - 1); // one small message arrives whole on loopback
    dino_socket_send(conn, buf, n);
    memset(buf, 0, sizeof(buf));
    n = dino_socket_recv(client, buf, sizeof(buf) - 1);
    printf("echoed %d bytes: %s\n", n, buf);

    dino_socket_close(conn);
    dino_socket_close(client);
    dino_socket_close(server);
    return strcmp(buf, msg) == 0 ? 0 : 1;
}
```

## Functions

| Function                                             | Does                                                                                                                                                   | Returns                                                 |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------- |
| `dino_socket_create(domain, type, protocol)`         | A new socket. `DINO_AF_INET` or `DINO_AF_INET6`; `DINO_SOCK_STREAM` with `DINO_IPPROTO_TCP`, or `DINO_SOCK_DGRAM` with `DINO_IPPROTO_UDP`.             | the socket, or `DINO_INVALID_SOCKET`                    |
| `dino_socket_close(sock)`                            | Close it and free the handle.                                                                                                                          | `0`, `-1`                                               |
| `dino_socket_bind(sock, ip, port)`                   | Bind to a local address.                                                                                                                               | `0`, `-1`                                               |
| `dino_socket_listen(sock, backlog)`                  | Start accepting connections, with up to `backlog` waiting.                                                                                             | `0`, `-1`                                               |
| `dino_socket_accept(sock)`                           | Take the next connection.                                                                                                                              | the new socket, or `DINO_INVALID_SOCKET`                |
| `dino_socket_connect(sock, ip, port)`                | Connect to a remote address.                                                                                                                           | `0`, `-1` on failure                                    |
| `dino_socket_send(sock, buf, len)`                   | Send up to `len` bytes. Never raises `SIGPIPE`.                                                                                                        | bytes sent, `-1` on error                               |
| `dino_socket_recv(sock, buf, len)`                   | Receive up to `len` bytes.                                                                                                                             | bytes received, `0` when the peer closed, `-1` on error |
| `dino_socket_shutdown(sock, how)`                    | Stop reading (`DINO_SHUT_RD`), writing (`DINO_SHUT_WR`) or both (`DINO_SHUT_RDWR`).                                                                    | `0`, `-1`                                               |
| `dino_socket_set_v6only(sock, enable)`               | IPv6 sockets only: `0` makes one dual-stack, so bound to `"::"` it also accepts IPv4 peers. They start v6-only.                                        | `0`, `-1`                                               |
| `dino_socket_set_reuseaddr(sock, enable)`            | `SO_REUSEADDR`, so a server can rebind its port right after restarting.                                                                                | `0`, `-1`                                               |
| `dino_socket_set_nonblocking(sock, enable)`          | Non-blocking mode: calls that would wait return `-1` at once instead.                                                                                  | `0`, `-1`                                               |
| `dino_socket_setopt(sock, level, optname, val, len)` | Any other option. `level` and `optname` are the platform's own constants (`SOL_SOCKET`, `TCP_NODELAY`, ...), so the call needs the platform's headers. | `0`, `-1`                                               |
| `dino_net_init()` / `dino_net_cleanup()`             | Socket subsystem setup and teardown. Both optional: see [runtime](runtime.md).                                                                         | `0` / -                                                 |

## Addresses

- **Literals only:** `ip` is a numeric address literal that matches the socket's domain, such as
  `"127.0.0.1"`, `"0.0.0.0"`, `"::1"` or `"::"`. Host names such as `"localhost"` are rejected,
  not resolved.
- **Any interface:** bind to `"0.0.0.0"` (IPv4) or `"::"` (IPv6).
- **IPv4 and IPv6 are separate.** An IPv6 socket is v6-only on every platform, so a server for
  both families binds two sockets, `"0.0.0.0"` and `"::"`, on the same port. Or it calls
  `dino_socket_set_v6only(sock, 0)` before binding one IPv6 socket to `"::"`, which then also
  accepts IPv4 peers (shown as `::ffff:a.b.c.d`). Linux and Windows default differently; the SDK
  sets it either way.
- **Ports** are plain integers in host byte order, `0` to `65535`; anything else fails with `-1`.
  Bind to port `0` for any free port.

## Pitfalls

- **Short transfers.** `send` may send fewer bytes than asked, and `recv` returns whatever has
  arrived: a message can come in pieces, or several can come together. Loop until you have what
  your protocol says you should.
- **Non-blocking sockets** return `-1` from `recv`, `send`, `accept` and `connect` when they would
  have waited. The API doesn't distinguish that from a real error, so use non-blocking sockets with
  [poll](io.md) or the [event loop](event.md), which tell you when a call won't wait.
- **Accepted sockets are blocking**, even from a non-blocking listener. Call
  `dino_socket_set_nonblocking` on each one you want non-blocking. Windows' `accept` would copy the
  listener's mode; the SDK clears it so both platforms agree.
- **Closing.** Close every socket exactly once. A socket registered with an event loop must be
  removed from it (`dino_event_del_socket`) before it's closed.
- **Wait for many sockets** with the [event loop](event.md), not one thread per connection.

See also: [`examples/network_example`](../../examples/network_example/main.c),
[`examples/ipv6_example`](../../examples/ipv6_example/main.c), and the header for every parameter.
