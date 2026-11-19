#ifndef DINO_CORE_NETWORK_H
#define DINO_CORE_NETWORK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

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
 * @param  domain       Socket domain (i.e., AF_INET)
 * @param  type         Socket type (i.e., SOCK_STREAM)
 * @param  protocol     Socket protocol (IPPROTO_TCP)
 * @return Socket, or DINO_INVALID_SOCKET on failure.
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
 * @return Client socket, or DINO_INVALID_SOCKET on failure.
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
 *         port is out of range.
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
 * @param  len      Maximum length of buf.
 * @return Number of bytes sent, or -1 if an error occurred (including a
 *         negative len).
 */
int dino_socket_send(dino_socket_t sock, const void *buf, int len);

/**
 * Wrapper for recv(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to receive information.
 * @param  buf      Buffer to store received information.
 * @param  len      Maximum length of buf.
 * @return Number of bytes received, or -1 if an error occurred (including a
 *         negative len).
 */
int dino_socket_recv(dino_socket_t sock, void *buf, int len);

typedef enum
{
    DINO_SHUT_RD   = 0,
    DINO_SHUT_WR   = 1,
    DINO_SHUT_RDWR = 2
} dino_socket_shutdown_t;

/**
 * Low-level socket option setter. Level and optname use platform values.
 *
 * @param  sock     Socket to configure.
 * @param  level    Protocol level (e.g. SOL_SOCKET, IPPROTO_TCP).
 * @param  optname  Option identifier.
 * @param  val      Pointer to option value.
 * @param  len      Size of val in bytes.
 * @return 0 on success, -1 on error.
 */
int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, int len);

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
 * that takeover, so there it only checks sock and succeeds.
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

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_NETWORK_H
