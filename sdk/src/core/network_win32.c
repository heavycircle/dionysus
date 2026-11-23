#include <dino/core/network.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include "error_internal.h"

typedef int dino_socklen_t;

// SIO_UDP_CONNRESET from mstcpip.h, which not every MinGW ships.
#define DINO__SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)

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
        return -1;
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
        return -1;
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
        return -1;
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
        // WSAStartup returns its error; it doesn't set the last-error value.
        WSADATA wsaData;
        int     err = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (err == 0)
            InterlockedExchange(&wsa_started, 1);
        else
            rc = dino__fail(dino__error_from_code(err));
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
    // A value outside the enums, such as a platform's own AF_INET6 passed by
    // habit, is refused rather than quietly made IPv4, stream or "any".
    if (os_domain < 0 || os_type < 0 || os_protocol < 0)
    {
        dino__error_set(DINO_EINVAL);
        return DINO_INVALID_SOCKET;
    }

    // Never inherited by child processes: a child holding a copy keeps a
    // listening port bound, or a connection open, after the parent closes it.
    // WSA_FLAG_OVERLAPPED keeps what socket() would give, for IOCP.
    SOCKET s = WSASocketW(
        os_domain, os_type, os_protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    // The OS default differs (dual-stack on Linux, v6-only on Windows).
    if (os_domain == AF_INET6 && dino_socket_set_v6only((dino_socket_t)s, 1) != 0)
    {
        closesocket(s);
        return DINO_INVALID_SOCKET;
    }
    // Windows reports an ICMP port-unreachable for an earlier datagram as
    // WSAECONNRESET from the next receive on an unconnected UDP socket, so a
    // server would fail whenever one client went away. Linux reports it only
    // on connected ones; SIO_UDP_CONNRESET off makes Windows agree.
    if (os_type == SOCK_DGRAM)
    {
        BOOL  report = FALSE;
        DWORD bytes;
        WSAIoctl(s, DINO__SIO_UDP_CONNRESET, &report, sizeof(report), NULL, 0, &bytes, NULL, NULL);
    }

    return (dino_socket_t)s;
}

int dino_socket_close(dino_socket_t sock)
{
    if (sock == DINO_INVALID_SOCKET)
        return dino__fail(DINO_EINVAL);

    if (closesocket((SOCKET)sock) == SOCKET_ERROR)
        return dino__fail_os();

    return 0;
}

// The interface a "%zone" suffix names: a number (Windows names interfaces by number: fe80::1%12).
static int parse_zone(const char *zone, unsigned long *scope)
{
    if (*zone >= '0' && *zone <= '9')
    {
        char         *end;
        unsigned long n = strtoul(zone, &end, 10);
        if (*end || n > 0xffffffffUL)
            return -1;
        *scope = n;
        return 0;
    }
    return -1;
}

// An IPv6 socket that also takes IPv4 (dino_socket_set_v6only(sock, 0)).
// getsockname fails on a socket not yet bound, so the family comes from the
// protocol info.
static int dual_stack(dino_socket_t sock)
{
    WSAPROTOCOL_INFOW info;
    int               len = sizeof(info);
    if (getsockopt((SOCKET)sock, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &len) != 0
        || info.iAddressFamily != AF_INET6)
        return 0;
    DWORD v6only = 1;
    len          = sizeof(v6only);
    return getsockopt((SOCKET)sock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&v6only, &len) == 0
        && !v6only;
}

// The address string picks the family, so one call works for both IPv4 and
// IPv6 sockets. A string that is neither fails instead of silently binding or
// connecting to 0.0.0.0, and so does a port that doesn't fit in 16 bits.
// An IPv6 literal may name its interface ("fe80::1%eth0"), which a link-local
// address needs. An IPv4 literal becomes ::ffff:a.b.c.d for a dual-stack
// socket, which takes no IPv4 address as such.
static int dino_sockaddr(struct sockaddr_storage *addr, dino_socklen_t *len, const char *ip,
    int port, dino_socket_t sock)
{
    struct sockaddr_in  *v4 = (struct sockaddr_in *)addr;
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)addr;

    memset(addr, 0, sizeof(*addr));
    if (ip == NULL || port < 0 || port > 65535)
        return -1;

    char        host[DINO_IP_STRLEN];
    const char *zone = strchr(ip, '%');
    size_t      n    = zone ? (size_t)(zone - ip) : strlen(ip);
    if (n >= sizeof(host))
        return -1;
    memcpy(host, ip, n);
    host[n] = '\0';

    if (!zone && inet_pton(AF_INET, host, &v4->sin_addr) == 1)
    {
        if (!dual_stack(sock))
        {
            v4->sin_family = AF_INET;
            v4->sin_port   = htons((unsigned short)port);
            *len           = (dino_socklen_t)sizeof(*v4);
            return 0;
        }
        unsigned char ipv4[4];
        memcpy(ipv4, &v4->sin_addr, 4);
        memset(addr, 0, sizeof(*addr));
        unsigned char *bytes = (unsigned char *)&v6->sin6_addr;
        bytes[10] = bytes[11] = 0xff;
        memcpy(bytes + 12, ipv4, 4);
    }
    else if (inet_pton(AF_INET6, host, &v6->sin6_addr) == 1)
    {
        unsigned long scope = 0;
        if (zone && parse_zone(zone + 1, &scope) != 0)
            return -1;
        v6->sin6_scope_id = (uint32_t)scope;
    }
    else
    {
        return -1;
    }
    v6->sin6_family = AF_INET6;
    v6->sin6_port   = htons((unsigned short)port);
    *len            = (dino_socklen_t)sizeof(*v6);
    return 0;
}

// The reverse of dino_sockaddr: the numeric form it accepts back.
static int dino_sockaddr_format(const struct sockaddr_storage *addr, dino_socket_addr_t *out)
{
    memset(out, 0, sizeof(*out));
    const void   *raw;
    unsigned long scope = 0;
    if (addr->ss_family == AF_INET)
    {
        const struct sockaddr_in *v4 = (const struct sockaddr_in *)addr;
        raw                          = &v4->sin_addr;
        out->port                    = ntohs(v4->sin_port);
    }
    else if (addr->ss_family == AF_INET6)
    {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)addr;
        raw                           = &v6->sin6_addr;
        out->port                     = ntohs(v6->sin6_port);
        scope                         = v6->sin6_scope_id;
    }
    else
    {
        return dino__fail(DINO_ENOTSUP);
    }
    if (!inet_ntop(addr->ss_family, raw, out->ip, sizeof(out->ip)))
        return dino__fail_os();
    // The interface as a number, which dino_sockaddr takes back on both platforms.
    if (scope)
    {
        size_t used = strlen(out->ip);
        snprintf(out->ip + used, sizeof(out->ip) - used, "%%%lu", scope);
    }
    return 0;
}

// A datagram larger than the buffer fails with WSAEMSGSIZE after filling it;
// Linux returns it cut to fit, which is what the SDK promises.
static int received_or_fail(int received, size_t len)
{
    if (received != SOCKET_ERROR)
        return received;
    if (WSAGetLastError() == WSAEMSGSIZE)
        return dino__clamp_len(len);
    return dino__fail_os();
}

int dino_socket_bind(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port, sock) != 0)
        return dino__fail(DINO_EINVAL);

    int res = bind((SOCKET)sock, (struct sockaddr *)&addr, len);

    return (res == SOCKET_ERROR) ? dino__fail_os() : 0;
}

int dino_socket_listen(dino_socket_t sock, int backlog)
{
    int res = listen((SOCKET)sock, backlog);

    return (res == SOCKET_ERROR) ? dino__fail_os() : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock)
{
    // Large enough for an IPv6 peer.
    struct sockaddr_storage addr;
    dino_socklen_t          len = sizeof(addr);

    SOCKET client = accept((SOCKET)sock, (struct sockaddr *)&addr, &len);
    if (client == INVALID_SOCKET)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    // Not documented to carry over from the listening socket. Non-blocking
    // mode does carry over on Windows; Linux's accept4 never does, so clear it.
    u_long blocking = 0;
    if (!SetHandleInformation((HANDLE)client, HANDLE_FLAG_INHERIT, 0)
        || ioctlsocket(client, FIONBIO, &blocking) != 0)
    {
        dino__error_set_os();
        closesocket(client);
        return DINO_INVALID_SOCKET;
    }

    return (dino_socket_t)client;
}

int dino_socket_connect(dino_socket_t sock, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          len;
    if (dino_sockaddr(&addr, &len, ip, port, sock) != 0)
        return dino__fail(DINO_EINVAL);

    int res = connect((SOCKET)sock, (struct sockaddr *)&addr, len);
    if (res != SOCKET_ERROR)
        return 0;
    // Winsock's word for a non-blocking connect that has started is "would
    // block"; everywhere else that is EINPROGRESS, and EAGAIN means try again.
    if (WSAGetLastError() == WSAEWOULDBLOCK)
        return dino__fail(DINO_EINPROGRESS);
    return dino__fail_os();
}

int dino_socket_send(dino_socket_t sock, const void *buf, size_t len)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    int sent = send((SOCKET)sock, (const char *)buf, dino__clamp_len(len), 0);
    return (sent == SOCKET_ERROR) ? dino__fail_os() : sent;
}

int dino_socket_recv(dino_socket_t sock, void *buf, size_t len)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    int received = recv((SOCKET)sock, (char *)buf, dino__clamp_len(len), 0);
    return received_or_fail(received, len);
}

int dino_socket_sendto(dino_socket_t sock, const void *buf, size_t len, const char *ip, int port)
{
    struct sockaddr_storage addr;
    dino_socklen_t          addr_len;
    if ((len > 0 && !buf) || dino_sockaddr(&addr, &addr_len, ip, port, sock) != 0)
        return dino__fail(DINO_EINVAL);
    int sent = sendto((SOCKET)sock, (const char *)buf, dino__clamp_len(len), 0,
        (struct sockaddr *)&addr, addr_len);
    return (sent == SOCKET_ERROR) ? dino__fail_os() : sent;
}

int dino_socket_recvfrom(dino_socket_t sock, void *buf, size_t len, dino_socket_addr_t *from)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    struct sockaddr_storage addr;
    dino_socklen_t          addr_len = sizeof(addr);
    int                     received = recvfrom(
        (SOCKET)sock, (char *)buf, dino__clamp_len(len), 0, (struct sockaddr *)&addr, &addr_len);
    received = received_or_fail(received, len);
    if (received < 0)
        return -1;
    if (from && dino_sockaddr_format(&addr, from) != 0)
        return -1;
    return received;
}

int dino_socket_local_addr(dino_socket_t sock, dino_socket_addr_t *addr)
{
    if (!addr)
        return dino__fail(DINO_EINVAL);
    struct sockaddr_storage raw;
    dino_socklen_t          len = sizeof(raw);
    if (getsockname((SOCKET)sock, (struct sockaddr *)&raw, &len) == SOCKET_ERROR)
        return dino__fail_os();
    return dino_sockaddr_format(&raw, addr);
}

int dino_socket_peer_addr(dino_socket_t sock, dino_socket_addr_t *addr)
{
    if (!addr)
        return dino__fail(DINO_EINVAL);
    struct sockaddr_storage raw;
    dino_socklen_t          len = sizeof(raw);
    if (getpeername((SOCKET)sock, (struct sockaddr *)&raw, &len) == SOCKET_ERROR)
        return dino__fail_os();
    return dino_sockaddr_format(&raw, addr);
}

int dino_socket_setopt(dino_socket_t sock, int level, int optname, const void *val, size_t len)
{
    if (len > (size_t)INT_MAX || (len > 0 && !val))
        return dino__fail(DINO_EINVAL);
    int rc = setsockopt((SOCKET)sock, level, optname, (const char *)val, (int)len);
    return (rc == SOCKET_ERROR) ? dino__fail_os() : 0;
}

// Windows already lets a server rebind its port while old connections linger,
// which is all SO_REUSEADDR is for on POSIX. Its own SO_REUSEADDR does more:
// a second socket may bind a port another socket is listening on and take its
// connections. So it is never set (as libuv does).
int dino_socket_set_reuseaddr(dino_socket_t sock, int enable)
{
    (void)enable;
    if (sock == DINO_INVALID_SOCKET)
        return dino__fail(DINO_EINVAL);
    int type, len = sizeof(type);
    return getsockopt((SOCKET)sock, SOL_SOCKET, SO_TYPE, (char *)&type, &len) == 0
        ? 0
        : dino__fail_os();
}

int dino_socket_set_v6only(dino_socket_t sock, int enable)
{
    // Some Winsock providers accept the option on an IPv4 socket; Linux
    // refuses it, so check the family here.
    WSAPROTOCOL_INFOW info;
    int               len = sizeof(info);
    if (getsockopt((SOCKET)sock, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&info, &len) != 0)
        return dino__fail_os();
    if (info.iAddressFamily != AF_INET6)
        return dino__fail(DINO_EINVAL);
    int flag = enable ? 1 : 0;
    return dino_socket_setopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag));
}

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
    u_long mode = enable ? 1 : 0;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode) == 0 ? 0 : dino__fail_os();
}

int dino_socket_shutdown(dino_socket_t sock, dino_socket_shutdown_t how)
{
    int rc = shutdown((SOCKET)sock, (int)how);
    return (rc == SOCKET_ERROR) ? dino__fail_os() : 0;
}

int dino_socket_error(dino_socket_t sock)
{
    int pending = 0;
    int len     = sizeof(pending);
    if (getsockopt((SOCKET)sock, SOL_SOCKET, SO_ERROR, (char *)&pending, &len) != 0)
        return dino__error_from_os();
    return pending ? dino__error_from_code(pending) : 0;
}
