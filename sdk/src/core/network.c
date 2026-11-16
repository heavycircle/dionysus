#include <dino/core/network.h>

#include <string.h>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

typedef int dino_socklen_t;

#else

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

typedef socklen_t dino_socklen_t;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

#endif // _WIN32

static int translate_domain(dino_domain_t domain)
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

static int translate_type(dino_type_t type)
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

static int translate_protocol(dino_protocol_t protocol)
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
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 1;
#endif // _WIN32

    return 0;
}

void dino_net_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
}

dino_socket_t dino_socket_create(dino_domain_t domain, dino_type_t type, dino_protocol_t protocol)
{
    int os_domain   = translate_domain(domain);
    int os_type     = translate_type(type);
    int os_protocol = translate_protocol(protocol);

    dino_socket_t s = (dino_socket_t)socket(os_domain, os_type, os_protocol);
    if (s == (dino_socket_t)INVALID_SOCKET)
        return DINO_INVALID_SOCKET;

    return s;
}

int dino_socket_close(dino_socket_t sock)
{
    if (sock == DINO_INVALID_SOCKET)
        return -1;

#ifdef _WIN32
    if (closesocket((SOCKET)sock) == SOCKET_ERROR)
        return -1;
#else
    if (close((int)sock) < 0)
        return -1;
#endif // _WIN32

    return 0;
}

// The address string picks the family, so one call works for both IPv4 and
// IPv6 sockets. A string that is neither fails instead of silently binding or
// connecting to 0.0.0.0.
static int dino_sockaddr(
    struct sockaddr_storage *addr, dino_socklen_t *len, const char *ip, int port)
{
    struct sockaddr_in  *v4 = (struct sockaddr_in *)addr;
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;

    memset(addr, 0, sizeof(*addr));
    if (ip == NULL)
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
        return -1;

#ifdef _WIN32
    int res = bind((SOCKET)sock, (struct sockaddr *)&addr, len);
#else
    int res = bind((int)sock, (struct sockaddr *)&addr, len);
#endif // _WIN32

    return (res == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_listen(dino_socket_t sock, int backlog)
{
#ifdef _WIN32
    int res = listen((SOCKET)sock, backlog);
#else
    int res = listen((int)sock, backlog);
#endif // _WIN32

    return (res == SOCKET_ERROR) ? -1 : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock)
{
    // Large enough for an IPv6 peer.
    struct sockaddr_storage addr;
    dino_socklen_t          len = sizeof(addr);

#ifdef _WIN32
    SOCKET client = accept((SOCKET)sock, (struct sockaddr *)&addr, &len);
#else
    int client = accept((int)sock, (struct sockaddr *)&addr, &len);
#endif // _WIN32

    if (client == INVALID_SOCKET)
        return DINO_INVALID_SOCKET;
    return (dino_socket_t)client;
}

int dino_socket_connect(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port) != 0)
        return -1;

#ifdef _WIN32
    int res = connect((SOCKET)sock, (struct sockaddr *)&addr, len);
#else
    int res = connect((int)sock, (struct sockaddr *)&addr, len);
#endif

    return (res == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_send(dino_socket_t sock, const void *buf, int len)
{
#ifdef _WIN32
    int sent = send((SOCKET)sock, (const char *)buf, len, 0);
#else
    // send() never returns more than len, so the result fits in an int.
    int sent = (int)send((int)sock, (const char *)buf, len, 0);
#endif
    return (sent == SOCKET_ERROR) ? -1 : sent;
}

int dino_socket_recv(dino_socket_t sock, void *buf, int len)
{
#ifdef _WIN32
    int received = recv((SOCKET)sock, (char *)buf, len, 0);
#else
    int received = (int)recv((int)sock, (char *)buf, len, 0);
#endif
    return (received == SOCKET_ERROR) ? -1 : received;
}

int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, int len)
{
#ifdef _WIN32
    int rc = setsockopt((SOCKET)sock, level, optname, (const char *)val, len);
#else
    int rc = setsockopt((int)sock, level, optname, val, (socklen_t)len);
#endif
    return (rc == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_set_reuseaddr(dino_socket_t sock, int enable)
{
    int flag = enable ? 1 : 0;
    return dino_socket_setopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
}

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl((int)sock, F_GETFL, 0);
    if (flags == -1)
        return -1;
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl((int)sock, F_SETFL, flags) == -1 ? -1 : 0;
#endif
}

int dino_socket_shutdown(dino_socket_t sock, dino_shutdown_how_t how)
{
#ifdef _WIN32
    int rc = shutdown((SOCKET)sock, (int)how);
#else
    int rc = shutdown((int)sock, (int)how);
#endif
    return (rc == SOCKET_ERROR) ? -1 : 0;
}
