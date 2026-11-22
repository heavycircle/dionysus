#include <dino/core/network.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "error_internal.h"

typedef socklen_t dino_socklen_t;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

static int translate_domain(dino_socket_domain_t domain)
{
    switch (domain)
    {
    case DINO_AF_INET:
        return AF_INET;
    case DINO_AF_INET6:
        return AF_INET6;
    default:
        return AF_INET;
    }
}

static int translate_type(dino_socket_type_t type)
{
    switch (type)
    {
    case DINO_SOCK_STREAM:
        return SOCK_STREAM;
    case DINO_SOCK_DGRAM:
        return SOCK_DGRAM;
    default:
        return SOCK_STREAM;
    }
}

static int translate_protocol(dino_socket_protocol_t protocol)
{
    switch (protocol)
    {
    case DINO_IPPROTO_TCP:
        return IPPROTO_TCP;
    case DINO_IPPROTO_UDP:
        return IPPROTO_UDP;
    default:
        return 0;
    }
}

int dino_net_init(void)
{
    return 0;
}

void dino_net_cleanup(void)
{
}

dino_socket_t dino_socket_create(
    dino_socket_domain_t domain, dino_socket_type_t type, dino_socket_protocol_t protocol)
{
    if (dino_net_init() != 0)
        return DINO_INVALID_SOCKET;

    int os_domain   = translate_domain(domain);
    int os_type     = translate_type(type);
    int os_protocol = translate_protocol(protocol);

    // Never inherited by child processes: a child holding a copy keeps a
    // listening port bound, or a connection open, after the parent closes it.
    int s = socket(os_domain, os_type | SOCK_CLOEXEC, os_protocol);
    if (s == INVALID_SOCKET)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    // The OS default differs (dual-stack on Linux, v6-only on Windows).
    if (os_domain == AF_INET6 && dino_socket_set_v6only((dino_socket_t)s, 1) != 0)
    {
        close(s);
        return DINO_INVALID_SOCKET;
    }

    return (dino_socket_t)s;
}

int dino_socket_close(dino_socket_t sock)
{
    if (sock == DINO_INVALID_SOCKET)
        return dino__fail(DINO_EINVAL);

    if (close((int)sock) < 0)
        return dino__fail_os();

    return 0;
}

// The address string picks the family, so one call works for both IPv4 and
// IPv6 sockets. A string that is neither fails instead of silently binding or
// connecting to 0.0.0.0, and so does a port that doesn't fit in 16 bits.
static int dino_sockaddr(
    struct sockaddr_storage *addr, dino_socklen_t *len, const char *ip, int port)
{
    struct sockaddr_in  *v4 = (struct sockaddr_in *)addr;
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;

    memset(addr, 0, sizeof(*addr));
    if (ip == NULL || port < 0 || port > 65535)
        return -1;

    if (inet_pton(AF_INET, ip, &v4->sin_addr) == 1)
    {
        v4->sin_family = AF_INET;
        v4->sin_port   = htons((unsigned short)port);
        *len           = (dino_socklen_t)sizeof(*v4);
        return 0;
    }
    if (inet_pton(AF_INET6, ip, &v6->sin6_addr) == 1)
    {
        v6->sin6_family = AF_INET6;
        v6->sin6_port   = htons((unsigned short)port);
        *len            = (dino_socklen_t)sizeof(*v6);
        return 0;
    }
    return -1;
}

int dino_socket_bind(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port) != 0)
        return dino__fail(DINO_EINVAL);

    int res = bind((int)sock, (struct sockaddr *)&addr, len);

    return (res == SOCKET_ERROR) ? dino__fail_os() : 0;
}

int dino_socket_listen(dino_socket_t sock, int backlog)
{
    int res = listen((int)sock, backlog);

    return (res == SOCKET_ERROR) ? dino__fail_os() : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock)
{
    // Large enough for an IPv6 peer.
    struct sockaddr_storage addr;
    dino_socklen_t          len = sizeof(addr);

    // A signal handler interrupts these calls unless it was installed with
    // SA_RESTART, and always when a socket timeout is set. Windows has no
    // signals: retry, as the pipe calls do.
    int client;
    do
        client = accept4((int)sock, (struct sockaddr *)&addr, &len, SOCK_CLOEXEC);
    while (client == INVALID_SOCKET && errno == EINTR);
    if (client == INVALID_SOCKET)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }

    return (dino_socket_t)client;
}

int dino_socket_connect(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port) != 0)
        return dino__fail(DINO_EINVAL);

    int res = connect((int)sock, (struct sockaddr *)&addr, len);
    if (res != SOCKET_ERROR)
        return 0;
    // An interrupted connect isn't undone: it goes on in the background and
    // ends as a non-blocking one does (dino_socket_error once writable).
    // Calling connect again would fail with EALREADY.
    if (errno == EINTR)
        return dino__fail(DINO_EINPROGRESS);
    return dino__fail_os();
}

int dino_socket_send(dino_socket_t sock, const void *buf, size_t len)
{
    // send() never returns more than len, so the result fits in an int.
    // MSG_NOSIGNAL: a closed peer fails the call with EPIPE, as on Windows,
    // instead of raising SIGPIPE, which would end the process.
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    int sent;
    do
        sent = (int)send((int)sock, (const char *)buf, (size_t)dino__clamp_len(len), MSG_NOSIGNAL);
    while (sent == SOCKET_ERROR && errno == EINTR);
    return (sent == SOCKET_ERROR) ? dino__fail_os() : sent;
}

int dino_socket_recv(dino_socket_t sock, void *buf, size_t len)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    int received;
    do
        received = (int)recv((int)sock, (char *)buf, (size_t)dino__clamp_len(len), 0);
    while (received == SOCKET_ERROR && errno == EINTR);
    return (received == SOCKET_ERROR) ? dino__fail_os() : received;
}

int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, size_t len)
{
    if (len > (size_t)INT_MAX || (len > 0 && !val))
        return dino__fail(DINO_EINVAL);
    int rc = setsockopt((int)sock, level, optname, val, (socklen_t)len);
    return (rc == SOCKET_ERROR) ? dino__fail_os() : 0;
}

// Stream sockets only. On a datagram socket Linux's SO_REUSEADDR lets a second
// socket bind the same port and take its datagrams, which Windows never
// allows through this call; there it does nothing, as on Windows.
int dino_socket_set_reuseaddr(dino_socket_t sock, int enable)
{
    int       type;
    socklen_t len = sizeof(type);
    if (getsockopt((int)sock, SOL_SOCKET, SO_TYPE, &type, &len) != 0)
        return dino__fail_os();
    if (type != SOCK_STREAM)
        return 0;
    int flag = enable ? 1 : 0;
    return dino_socket_setopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
}

int dino_socket_set_v6only(dino_socket_t sock, int enable)
{
    int flag = enable ? 1 : 0;
    return dino_socket_setopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag));
}

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
    int flags = fcntl((int)sock, F_GETFL, 0);
    if (flags == -1)
        return dino__fail_os();
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl((int)sock, F_SETFL, flags) == -1 ? dino__fail_os() : 0;
}

int dino_socket_shutdown(dino_socket_t sock, dino_socket_shutdown_t how)
{
    int rc = shutdown((int)sock, (int)how);
    return (rc == SOCKET_ERROR) ? dino__fail_os() : 0;
}

int dino_socket_error(dino_socket_t sock)
{
    int       pending = 0;
    socklen_t len     = sizeof(pending);
    if (getsockopt((int)sock, SOL_SOCKET, SO_ERROR, &pending, &len) != 0)
        return dino__error_from_os();
    return pending ? dino__error_from_code(pending) : 0;
}
