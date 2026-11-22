#ifndef DINO_CORE_NETWORK_H
#define DINO_CORE_NETWORK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * A socket: the operating system's own, the descriptor on Linux and the
 * SOCKET on Windows, so it can be passed to platform calls. Unlike the other
 * handles it is an integer; they are pointer types, so none of them can be
 * passed where a socket is expected.
 */
typedef uintptr_t dino_socket_t;

#define DINO_INVALID_SOCKET ((dino_socket_t)(~0))

typedef enum
{
    DINO_AF_INET,
    DINO_AF_INET6
} dino_socket_domain_t;

typedef enum
{
    DINO_SOCK_STREAM,
    DINO_SOCK_DGRAM
} dino_socket_type_t;

typedef enum
{
    DINO_IPPROTO_TCP,
    DINO_IPPROTO_UDP
} dino_socket_protocol_t;

/**
 * Initialize environment for making sockets (WSAStartup on Windows).
 *
 * Optional: dino_socket_create() does this on first use and returns
 * DINO_INVALID_SOCKET if it fails. Call it to check for failure up front. Safe
 * to call any number of times, from any thread; only the first successful call
 * does anything.
 *
 * @return 0 on success, -1 on failure.
 */
int dino_net_init(void);

/**
 * Teardown environment after socket connections (WSACleanup on Windows).
 *
 * Optional: the OS releases everything at exit. No sockets may be in use. Later
 * socket calls initialize again.
 */
void dino_net_cleanup(void);

/**
 * Create a socket.
 *
 * A DINO_AF_INET6 socket is v6-only on every platform; see
 * dino_socket_set_v6only for one that also handles IPv4.
 *
 * @param  domain       DINO_AF_INET or DINO_AF_INET6.
 * @param  type         DINO_SOCK_STREAM or DINO_SOCK_DGRAM.
 * @param  protocol     DINO_IPPROTO_TCP or DINO_IPPROTO_UDP.
 * @return Socket, or DINO_INVALID_SOCKET on failure, including for a value
 *         outside these enums (a platform's own AF_INET6, say).
 */
dino_socket_t dino_socket_create(
    dino_socket_domain_t domain, dino_socket_type_t type, dino_socket_protocol_t protocol);

/**
 * Close the socket connection and free the handle.
 *
 * @param  sock     Socket to close.
 * @return 0 on success, -1 on failure.
 */
int dino_socket_close(dino_socket_t sock);

/**
 * Wrapper for bind(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to bind to the port.
 * @param  ip       IPv4 or IPv6 address literal to bind to, matching the socket's domain
 *                  (e.g. "0.0.0.0", "::", "::1"). Host names are not resolved.
 * @param  port     Port to bind to, 0 to 65535 (0 picks any free port).
 * @return 0 on success, -1 on error, if ip is not a valid address or if port
 *         is out of range.
 */
int dino_socket_bind(dino_socket_t sock, const char *ip, int port);

/**
 * Wrapper for listen(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to listen for connections.
 * @param  backlog  Maximum length of the pending connection queue.
 * @return 0 on success, -1 on error.
 */
int dino_socket_listen(dino_socket_t sock, int backlog);

/**
 * Wrapper for accept(2) supporting Windows and POSIX APIs.
 *
 * The new socket is blocking even when the listener is non-blocking (Windows
 * would copy the listener's mode; this doesn't, so both platforms agree). Call
 * dino_socket_set_nonblocking on it for an event loop.
 *
 * @param  sock     Socket to accept connection.
 * @return Client socket, or DINO_INVALID_SOCKET on failure. On a non-blocking
 *         listener with no connection waiting, dino_error() is DINO_EAGAIN;
 *         any other code is a failure with the connection, if there was one,
 *         still waiting (DINO_EMFILE when the process is out of descriptors).
 */
dino_socket_t dino_socket_accept(dino_socket_t sock);

/**
 * Wrapper for connect(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to use for connection.
 * @param  ip       IPv4 or IPv6 address literal to connect to, matching the socket's domain.
 *                  Host names are not resolved.
 * @param  port     Port to connect to, 0 to 65535.
 * @return 0 on connection, -1 on error, if ip is not a valid address or if
 *         port is out of range. On a non-blocking socket -1 with dino_error()
 *         of DINO_EINPROGRESS means the connect has started: wait for the
 *         socket to become writable, then ask dino_socket_error() how it ended.
 */
int dino_socket_connect(dino_socket_t sock, const char *ip, int port);

/**
 * Wrapper for send(2) supporting Windows and POSIX APIs.
 *
 * Sending to a peer that has closed the connection returns -1 and never raises
 * SIGPIPE, so the process survives on every platform.
 *
 * @param  sock     Socket to send information.
 * @param  buf      Buffer to send to sock.
 * @param  len      Number of bytes in buf. One call sends at most INT_MAX
 *                  bytes; send the rest with another call, as for any
 *                  short send.
 * @return Number of bytes sent, or -1 if an error occurred. On a non-blocking socket whose send
 * buffer is full, -1 with dino_error() of DINO_EAGAIN; DINO_EPIPE or DINO_ECONNRESET when the peer
 * is gone.
 */
int dino_socket_send(dino_socket_t sock, const void *buf, size_t len);

/**
 * Wrapper for recv(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to receive information.
 * @param  buf      Buffer to store received information.
 * @param  len      Size of buf. One call receives at most INT_MAX bytes.
 * @return Number of bytes received, 0 when the peer has closed, or -1 if an
 *         error occurred. On a non-blocking socket
 *         with nothing to read, -1 with dino_error() of DINO_EAGAIN.
 */
int dino_socket_recv(dino_socket_t sock, void *buf, size_t len);

typedef enum
{
    DINO_SHUT_RD   = 0,
    DINO_SHUT_WR   = 1,
    DINO_SHUT_RDWR = 2
} dino_socket_shutdown_t;

/**
 * Low-level socket option setter. Level and optname use platform values, and
 * the value is passed on as given: options whose type differs by platform
 * (SO_RCVTIMEO and SO_SNDTIMEO take a struct timeval on Linux and a DWORD of
 * milliseconds on Windows; SO_LINGER) still need code per platform. What it
 * does remove is setsockopt's const char * value type on Windows.
 *
 * @param  sock     Socket to configure.
 * @param  level    Protocol level (e.g. SOL_SOCKET, IPPROTO_TCP).
 * @param  optname  Option identifier.
 * @param  val      Pointer to option value.
 * @param  len      Size of val in bytes.
 * @return 0 on success, -1 on error.
 */
int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, size_t len);

/**
 * Choose whether an IPv6 socket also handles IPv4.
 *
 * DINO_AF_INET6 sockets start v6-only on every platform (the OS defaults
 * differ: dual-stack on Linux, v6-only on Windows). With enable 0 the socket
 * is dual-stack: bound to "::" it also accepts IPv4 peers, which appear as
 * IPv4-mapped addresses (::ffff:a.b.c.d), and it holds the port for IPv4 too.
 * Call it before bind or connect.
 *
 * @param  sock    A DINO_AF_INET6 socket.
 * @param  enable  Non-zero for v6-only (the default), 0 for dual-stack.
 * @return 0 on success, -1 on error (including a socket that isn't IPv6).
 */
int dino_socket_set_v6only(dino_socket_t sock, int enable);

/**
 * Let a server bind its port again right after restarting, while connections
 * from the old process linger (SO_REUSEADDR on POSIX). Call before bind.
 *
 * It never lets a second socket bind a port another socket is listening on.
 * Windows allows the restart case by default and its SO_REUSEADDR would allow
 * that takeover, so there it only checks sock and succeeds. On a datagram
 * socket it does nothing, on every platform: Linux's SO_REUSEADDR would let a
 * second socket bind the same port and receive its datagrams.
 *
 * Two TCP sockets that both use it can still bind the same port on Linux as
 * long as neither listens; the second listen fails. On Windows the second
 * bind fails.
 *
 * @param  sock    Socket to configure.
 * @param  enable  Non-zero to enable, 0 to disable.
 * @return 0 on success, -1 on error.
 */
int dino_socket_set_reuseaddr(dino_socket_t sock, int enable);

/**
 * Set sock to non-blocking or blocking mode.
 *
 * Linux: uses fcntl(F_SETFL, O_NONBLOCK).
 * Windows: uses ioctlsocket(FIONBIO).
 *
 * @param  sock    Socket to configure.
 * @param  enable  Non-zero for non-blocking, 0 for blocking.
 * @return 0 on success, -1 on error.
 */
int dino_socket_set_nonblocking(dino_socket_t sock, int enable);

/**
 * Wrapper for shutdown(2) supporting Windows and POSIX APIs.
 *
 * @param  sock  Socket to shut down.
 * @param  how   Direction to shut down (DINO_SHUT_RD/WR/RDWR).
 * @return 0 on success, -1 on error.
 */
int dino_socket_shutdown(dino_socket_t sock, dino_socket_shutdown_t how);

/**
 * The error waiting on a socket (SO_ERROR).
 *
 * This is how a non-blocking connect ends: once the socket is writable, or an
 * event loop reports it, 0 here means it is connected and anything else is why
 * it isn't (DINO_ECONNREFUSED, DINO_ETIMEDOUT, DINO_EUNREACH, ...). Ask once:
 * Linux forgets the error when it is read, Windows may report it again.
 *
 * @param  sock  Socket to ask.
 * @return 0 if no error is waiting, otherwise a DINO_E* code from
 *         dino/core/error.h; DINO_EINVAL if sock is not a socket.
 */
int dino_socket_error(dino_socket_t sock);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_NETWORK_H
