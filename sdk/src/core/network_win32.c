#include <dino/core/network.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include "error_internal.h"

// SIO_UDP_CONNRESET from mstcpip.h, which not every MinGW ships.
#define DINO__SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif

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

dino_socket_t dino_socket_create(int domain, int type, int protocol)
{
    if (dino_net_init() != 0)
        return DINO_INVALID_SOCKET;

    // Never inherited by child processes: a child holding a copy keeps a
    // listening port bound, or a connection open, after the parent closes it.
    // WSA_FLAG_OVERLAPPED keeps what socket() would give, for IOCP.
    SOCKET s = WSASocketW(
        domain, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    // The OS default differs (dual-stack on Linux, v6-only on Windows): set it
    // anyway, so a later change of default can't make the two disagree.
    DWORD on = 1;
    if (domain == AF_INET6
        && setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&on, sizeof(on)) != 0)
    {
        dino__error_set_os();
        closesocket(s);
        return DINO_INVALID_SOCKET;
    }
    // Windows reports an ICMP port-unreachable for an earlier datagram as
    // WSAECONNRESET from the next receive on an unconnected UDP socket, so a
    // server would fail whenever one client went away. Linux reports it only
    // on connected ones; SIO_UDP_CONNRESET off makes Windows agree.
    if (type == SOCK_DGRAM)
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
    return closesocket((SOCKET)sock) == SOCKET_ERROR ? dino__fail_os() : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock, struct sockaddr *addr, socklen_t *len)
{
    if (addr && !len)
    {
        dino__error_set(DINO_EINVAL);
        return DINO_INVALID_SOCKET;
    }
    SOCKET client = accept((SOCKET)sock, addr, addr ? len : NULL);
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

int dino_socket_connect(dino_socket_t sock, const struct sockaddr *addr, socklen_t len)
{
    if (!addr)
        return dino__fail(DINO_EINVAL);
    if (connect((SOCKET)sock, addr, len) != SOCKET_ERROR)
        return 0;
    // Winsock's word for a non-blocking connect that has started is "would
    // block"; everywhere else that is EINPROGRESS, and EAGAIN means try again.
    if (WSAGetLastError() == WSAEWOULDBLOCK)
        return dino__fail(DINO_EINPROGRESS);
    return dino__fail_os();
}

int dino_socket_send(dino_socket_t sock, const void *buf, size_t len, int flags)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    int sent = send((SOCKET)sock, (const char *)buf, dino__clamp_len(len), flags);
    return sent == SOCKET_ERROR ? dino__fail_os() : sent;
}

int dino_socket_recvfrom(
    dino_socket_t sock, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if ((len > 0 && !buf) || (from && !fromlen))
        return dino__fail(DINO_EINVAL);
    int received = recvfrom(
        (SOCKET)sock, (char *)buf, dino__clamp_len(len), flags, from, from ? fromlen : NULL);
    if (received != SOCKET_ERROR)
        return received;
    // A datagram larger than the buffer fails with WSAEMSGSIZE after filling
    // it; Linux returns it cut to fit, which is what the SDK promises.
    if (WSAGetLastError() == WSAEMSGSIZE)
        return dino__clamp_len(len);
    return dino__fail_os();
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

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
    u_long mode = enable ? 1 : 0;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode) == 0 ? 0 : dino__fail_os();
}

int dino_socket_error(dino_socket_t sock)
{
    int pending = 0;
    int len     = sizeof(pending);
    if (getsockopt((SOCKET)sock, SOL_SOCKET, SO_ERROR, (char *)&pending, &len) != 0)
        return dino__error_from_os();
    return pending ? dino__error_from_code(pending) : 0;
}

int dino_socket_last_error(void)
{
    return dino__error_from_code(WSAGetLastError());
}
