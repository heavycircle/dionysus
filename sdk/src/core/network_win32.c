#include <dino/core/network.h>

#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

typedef int dino_socklen_t;

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif

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

// Winsock is started on first use rather than once for good, so that a
// cleanup followed by more socket calls starts it again.
static SRWLOCK       wsa_lock = SRWLOCK_INIT;
static volatile LONG wsa_started;

int dino_net_init(void)
{
    if (InterlockedCompareExchange(&wsa_started, 0, 0))
        return 0;

    int rc = 0;
    AcquireSRWLockExclusive(&wsa_lock);
    if (!wsa_started)
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0)
            InterlockedExchange(&wsa_started, 1);
        else
            rc = -1;
    }
    ReleaseSRWLockExclusive(&wsa_lock);
    return rc;
}

void dino_net_cleanup(void)
{
    AcquireSRWLockExclusive(&wsa_lock);
    if (wsa_started)
    {
        WSACleanup();
        InterlockedExchange(&wsa_started, 0);
    }
    ReleaseSRWLockExclusive(&wsa_lock);
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
    // WSA_FLAG_OVERLAPPED keeps what socket() would give, for IOCP.
    SOCKET s = WSASocketW(
        os_domain, os_type, os_protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET)
        return DINO_INVALID_SOCKET;

    return (dino_socket_t)s;
}

int dino_socket_close(dino_socket_t sock)
{
    if (sock == DINO_INVALID_SOCKET)
        return -1;

    if (closesocket((SOCKET)sock) == SOCKET_ERROR)
        return -1;

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

    int res = bind((SOCKET)sock, (struct sockaddr *)&addr, len);

    return (res == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_listen(dino_socket_t sock, int backlog)
{
    int res = listen((SOCKET)sock, backlog);

    return (res == SOCKET_ERROR) ? -1 : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock)
{
    // Large enough for an IPv6 peer.
    struct sockaddr_storage addr;
    dino_socklen_t          len = sizeof(addr);

    SOCKET client = accept((SOCKET)sock, (struct sockaddr *)&addr, &len);
    if (client == INVALID_SOCKET)
        return DINO_INVALID_SOCKET;
    // Not documented to carry over from the listening socket. Non-blocking
    // mode does carry over on Windows; Linux's accept4 never does, so clear it.
    u_long blocking = 0;
    if (!SetHandleInformation((HANDLE)client, HANDLE_FLAG_INHERIT, 0)
        || ioctlsocket(client, FIONBIO, &blocking) != 0)
    {
        closesocket(client);
        return DINO_INVALID_SOCKET;
    }

    return (dino_socket_t)client;
}

int dino_socket_connect(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port) != 0)
        return -1;

    int res = connect((SOCKET)sock, (struct sockaddr *)&addr, len);

    return (res == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_send(dino_socket_t sock, const void *buf, int len)
{
    if (len < 0 || (len > 0 && !buf))
        return -1;
    int sent = send((SOCKET)sock, (const char *)buf, len, 0);
    return (sent == SOCKET_ERROR) ? -1 : sent;
}

int dino_socket_recv(dino_socket_t sock, void *buf, int len)
{
    if (len < 0 || (len > 0 && !buf))
        return -1;
    int received = recv((SOCKET)sock, (char *)buf, len, 0);
    return (received == SOCKET_ERROR) ? -1 : received;
}

int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, int len)
{
    int rc = setsockopt((SOCKET)sock, level, optname, (const char *)val, len);
    return (rc == SOCKET_ERROR) ? -1 : 0;
}

int dino_socket_set_reuseaddr(dino_socket_t sock, int enable)
{
    int flag = enable ? 1 : 0;
    return dino_socket_setopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
}

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
    u_long mode = enable ? 1 : 0;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode) == 0 ? 0 : -1;
}

int dino_socket_shutdown(dino_socket_t sock, dino_socket_shutdown_t how)
{
    int rc = shutdown((SOCKET)sock, (int)how);
    return (rc == SOCKET_ERROR) ? -1 : 0;
}
