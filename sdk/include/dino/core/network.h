#ifndef DINO_CORE_NETWORK_H
#define DINO_CORE_NETWORK_H

#include <stddef.h>
#include <stdint.h>

// The POSIX socket headers on every target: on Windows the SDK provides them
// over Winsock (sdk/portability/win32-posix), so struct sockaddr, socklen_t,
// AF_INET6, getaddrinfo and the rest are spelled the same everywhere.
#include <sys/socket.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Sockets are the POSIX calls on every target, on a dino_socket_t, except for
// the calls below, which differ in ways the standard names can't hide: a
// Windows socket isn't a file descriptor, send raises SIGPIPE on Linux, and
// inheritance, would-block errors and oversized datagrams differ.

/**
 * A socket: the operating system's own, an int descriptor on Linux and a
 * SOCKET (an unsigned pointer-sized integer) on Windows, so it passes to the
 * POSIX calls (bind, listen, recv, sendto, setsockopt, getsockname, shutdown,
 * poll, ...) without a cast. Unlike the other handles it is an integer; they
 * are pointer types, so none of them can be passed where a socket is
 * expected.
 */
#ifdef _WIN32
typedef uintptr_t dino_socket_t;
#else
typedef int dino_socket_t;
#endif

#define DINO_INVALID_SOCKET ((dino_socket_t)(~0))

/**
 * Start the socket subsystem now (WSAStartup on Windows).
 *
 * Optional: it starts on its own before main() in a program that includes a
 * socket header, and dino_socket_create() starts it on first use. Call it to
 * fail early with a clear message. Safe to call any number of times, from any
 * thread.
 *
 * @return 0 on success, -1 on failure.
 */
int dino_net_init(void);

/**
 * Create a socket, as socket(2) does.
 *
 * Never inherited by child processes. An AF_INET6 socket is v6-only on every
 * platform (the defaults differ); clear IPV6_V6ONLY with setsockopt for one
 * that also takes IPv4.
 *
 * @param  domain    AF_INET or AF_INET6.
 * @param  type      SOCK_STREAM or SOCK_DGRAM.
 * @param  protocol  0, IPPROTO_TCP or IPPROTO_UDP.
 * @return Socket, or DINO_INVALID_SOCKET on failure.
 */
dino_socket_t dino_socket_create(int domain, int type, int protocol);

/**
 * Close the socket and free it: close(2) on Linux, closesocket on Windows.
 *
 * Not a way to stop another thread blocked in an accept or receive on it:
 * Windows wakes that thread, Linux doesn't (there shutdown(2) does, and the
 * descriptor may meanwhile be reused). To stop such a thread, wait with poll
 * or the event loop instead of blocking (a receive timeout doesn't apply to
 * accept on Windows).
 *
 * @param  sock  Socket to close.
 * @return 0 on success, -1 on failure.
 */
int dino_socket_close(dino_socket_t sock);

/**
 * Accept a connection, as accept(2) does. The new socket isn't inherited by
 * child processes, and a signal doesn't interrupt the wait.
 *
 * @param  sock  Listening socket.
 * @param  addr  Receives the peer's address; may be NULL.
 * @param  len   In: the size of addr. Out: the address's length. May be NULL
 *               when addr is.
 * @return The connected socket, or DINO_INVALID_SOCKET on failure.
 */
dino_socket_t dino_socket_accept(dino_socket_t sock, struct sockaddr *addr, socklen_t *len);

/**
 * Connect, as connect(2) does.
 *
 * On a non-blocking socket, or when a signal interrupts it, the connection
 * goes on in the background: -1 with DINO_EINPROGRESS (Linux says EINPROGRESS,
 * Windows WSAEWOULDBLOCK). Wait until the socket is writable, then ask
 * dino_socket_error.
 *
 * @param  sock  Socket to connect.
 * @param  addr  Address, from getaddrinfo or filled in by hand.
 * @param  len   Its length.
 * @return 0 when connected, -1 on failure or while in progress.
 */
int dino_socket_connect(dino_socket_t sock, const struct sockaddr *addr, socklen_t len);

#ifndef DINO_TIMEOUT
/** Returned when the time ran out (as in dino/core/thread.h). */
#define DINO_TIMEOUT 1
#endif

/**
 * Connect, giving up after timeout_ms. A plain blocking connect waits as long
 * as the system's own limit, about 21 seconds on Windows and two minutes or
 * more on Linux. Since 1.0.
 *
 * The socket is blocking afterwards, whatever it was before: Windows can't say
 * which mode a socket is in, so the mode can't be put back. After
 * DINO_TIMEOUT or a failure, close the socket; the connection attempt may
 * still be going on in it.
 *
 * @param  sock        Socket to connect.
 * @param  addr        Address, from getaddrinfo or filled in by hand.
 * @param  len         Its length.
 * @param  timeout_ms  Milliseconds to wait; -1 for as long as the system
 *                     allows, as a blocking connect waits; 0 to check once.
 * @return 0 when connected, DINO_TIMEOUT when the time ran out (timeout_ms,
 *         or the system's own limit), -1 on failure (dino_error():
 *         DINO_ECONNREFUSED, DINO_EUNREACH, ...).
 */
int dino_socket_connect_timeout(
    dino_socket_t sock, const struct sockaddr *addr, socklen_t len, int timeout_ms);

/**
 * Send, as send(2) does, without raising SIGPIPE on Linux: a closed peer
 * fails the call with DINO_EPIPE on every platform.
 *
 * @param  sock   Connected socket.
 * @param  buf    Data to send.
 * @param  len    Its length; more than INT_MAX is sent as INT_MAX.
 * @param  flags  send(2) flags, such as MSG_OOB; usually 0.
 * @return Bytes sent (possibly fewer than len), or -1 on failure.
 */
int dino_socket_send(dino_socket_t sock, const void *buf, size_t len, int flags);

/**
 * Receive a datagram or stream data, as recvfrom(2) does on Linux.
 *
 * On Windows a datagram larger than buf fails the call and an unconnected UDP
 * socket fails a receive after sending to a port nobody listens on; here, as
 * on Linux, the datagram comes back cut to fit and the stray error is skipped.
 *
 * @param  sock     Socket to receive on.
 * @param  buf      Destination.
 * @param  len      Its size; more than INT_MAX is treated as INT_MAX.
 * @param  flags    recv(2) flags, such as MSG_PEEK; usually 0.
 * @param  from     Receives the sender's address; may be NULL.
 * @param  fromlen  In: the size of from. Out: the address's length. May be
 *                  NULL when from is.
 * @return Bytes received (0 at a stream's end), or -1 on failure.
 */
int dino_socket_recvfrom(dino_socket_t sock, void *buf, size_t len, int flags,
    struct sockaddr *from, socklen_t *fromlen);

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
 * Limit how long a blocking receive or send on sock waits: SO_RCVTIMEO and
 * SO_SNDTIMEO, which take a struct timeval on POSIX and a DWORD on Windows.
 * Since 1.0.
 *
 * A dino_socket_recvfrom or dino_socket_send that runs out of time fails with
 * DINO_ETIMEDOUT on every platform (raw recv() says EAGAIN on Linux and
 * WSAETIMEDOUT on Windows). Then close the socket: Windows leaves it in an
 * undefined state, where data may have been lost or only partly sent.
 *
 * Receives and sends only. Linux also applies the receive limit to accept
 * and the send limit to a blocking connect, Windows to neither: for those, use
 * poll, the event loop or dino_socket_connect_timeout.
 *
 * @param  sock     Socket to configure.
 * @param  recv_ms  Milliseconds a receive may wait; -1 for no limit (the
 *                  default). 0 is refused: it would mean no limit on Linux
 *                  and on Windows alike, not "check once".
 * @param  send_ms  The same for a send.
 * @return 0 on success, -1 on error (DINO_EINVAL for a value below 1 other
 *         than -1).
 */
int dino_socket_set_timeouts(dino_socket_t sock, int recv_ms, int send_ms);

/**
 * Set sock to non-blocking or blocking mode: fcntl(O_NONBLOCK) on Linux,
 * ioctlsocket(FIONBIO) on Windows, which has no fcntl.
 *
 * @param  sock    Socket to configure.
 * @param  enable  Non-zero for non-blocking, 0 for blocking.
 * @return 0 on success, -1 on error.
 */
int dino_socket_set_nonblocking(dino_socket_t sock, int enable);

/**
 * The error waiting on a socket (SO_ERROR), as a DINO_E* code.
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

/**
 * Why the last POSIX socket call on this thread failed (bind, listen, recv,
 * sendto, setsockopt, getaddrinfo's sockets, ...), as a DINO_E* code: Linux
 * reports it in errno, Windows through WSAGetLastError. Read it right after
 * the call fails. The dino_socket_* calls report through dino_error() instead.
 *
 * @return A DINO_E* code from dino/core/error.h.
 */
int dino_socket_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_NETWORK_H
