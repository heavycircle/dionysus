#ifndef DINO_CORE_NETWORK_H
#define DINO_CORE_NETWORK_H

#include <stdint.h>

typedef uintptr_t dino_socket_t;

#define DINO_INVALID_SOCKET ((dino_socket_t)(~0))

typedef enum
{
    DINO_AF_INET,
    DINO_AF_INET6
} dino_domain_t;

typedef enum
{
    DINO_SOCK_STREAM,
    DINO_SOCK_DGRAM
} dino_type_t;

typedef enum
{
    DINO_IPPROTO_TCP,
    DINO_IPPROTO_UDP
} dino_protocol_t;

/**
 * Initialize environment for making sockets.
 *
 * Must be run before any other network functions are used.
 *
 * @return 0 on success, non-zero on failure.
 */
int dino_net_init(void);

/**
 * Teardown environment after socket connections.
 */
void dino_net_cleanup(void);

/**
 * Create a socket.
 *
 * @param  domain       Socket domain (i.e., AF_INET)
 * @param  type         Socket type (i.e., SOCK_STREAM)
 * @param  protocol     Socket protocol (IPPROTO_TCP)
 * @return Socket, or DINO_INVALID_SOCKET on failure.
 */
dino_socket_t dino_socket_create(dino_domain_t domain, dino_type_t type, dino_protocol_t protocol);

/**
 * Close the socket connection and free the handle.
 *
 * @param  sock     Socket to close.
 * @return 0 on success, non-zero on failure.
 */
int dino_socket_close(dino_socket_t sock);

/**
 * Wrapper for bind(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to bind to the port.
 * @param  ip       IP to bind to. Normally 0.0.0.0.
 * @param  port     Port to bind to.
 * @return 0 on success, negative on error.
 */
int dino_socket_bind(dino_socket_t sock, const char *ip, int port);

/**
 * Wrapper for listen(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to listen for connections.
 * @param  backlog  Maximum length of the pending connection queue.
 * @return 0 on success, negative on error.
 */
int dino_socket_listen(dino_socket_t sock, int backlog);

/**
 * Wrapper for accept(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to accept connection.
 * @return Client socket.
 */
dino_socket_t dino_socket_accept(dino_socket_t sock);

/**
 * Wrapper for connect(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to use for connection.
 * @param  ip       Address to connect to.
 * @param  port     Port to connect to.
 * @return Zero on connection, -1 on error.
 */
int dino_socket_connect(dino_socket_t sock, const char *ip, int port);

/**
 * Wrapper for send(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to send information.
 * @param  buf      Buffer to send to sock.
 * @param  len      Maximum length of buf.
 * @return Number of bytes sent, or -1 if an error occurred.
 */
int dino_socket_send(dino_socket_t sock, const void *buf, int len);

/**
 * Wrapper for recv(2) supporting Windows and POSIX APIs.
 *
 * @param  sock     Socket to receive information.
 * @param  buf      Buffer to store received information.
 * @param  len      Maximum length of buf.
 * @return Number of bytes received, or -1 if an error occurred.
 */
int dino_socket_recv(dino_socket_t sock, void *buf, int len);

#endif // DINO_CORE_NETWORK_H
