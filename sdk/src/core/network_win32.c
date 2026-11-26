#include <dino/core/network.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>

#include <dino/core/time.h>

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

int dino_socket_connect_timeout(
    dino_socket_t sock, const struct sockaddr *addr, socklen_t len, int timeout_ms)
{
    if (!addr || timeout_ms < -1)
        return dino__fail(DINO_EINVAL);
    if (dino_socket_set_nonblocking(sock, 1) != 0)
        return -1;
    int rc = 0;
    if (connect((SOCKET)sock, addr, len) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            rc = dino__fail_os();
        }
        else
        {
            // select, not WSAPoll: before Windows 10 2004 WSAPoll never
            // reports a connect that failed. A failure lands in the except set.
            // select can return up to a timer tick early, so the time left is
            // measured on the monotonic clock and a wait cut short repeated,
            // as poll's is on POSIX.
            uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
            fd_set   writable, failed;
            int      ready;
            for (;;)
            {
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET((SOCKET)sock, &writable);
                FD_SET((SOCKET)sock, &failed);
                struct timeval tv = { 0, 0 };
                if (timeout_ms >= 0)
                {
                    uint64_t now  = dino_time_monotonic_ns();
                    uint64_t left = now >= deadline ? 0 : (deadline - now + 999) / 1000;
                    tv.tv_sec     = (long)(left / 1000000);
                    tv.tv_usec    = (long)(left % 1000000);
                }
                ready = select(0, NULL, &writable, &failed, timeout_ms < 0 ? NULL : &tv);
                if (ready != 0 || timeout_ms < 0 || dino_time_monotonic_ns() >= deadline)
                    break;
            }
            if (ready == SOCKET_ERROR)
                rc = dino__fail_os();
            else if (ready == 0)
                rc = DINO_TIMEOUT;
            else
            {
                // The system's own limit running out is a timeout too.
                int code = dino_socket_error(sock);
                rc       = code == DINO_ETIMEDOUT ? DINO_TIMEOUT : code ? dino__fail(code) : 0;
            }
        }
    }
    // Blocking again, keeping the reason a failure was reported.
    int code = dino_error();
    dino_socket_set_nonblocking(sock, 0);
    dino__error_set(code);
    return rc;
}

// A DWORD of milliseconds, where POSIX takes a struct timeval; 0 is no limit
// on both.
int dino_socket_set_timeouts(dino_socket_t sock, int recv_ms, int send_ms)
{
    // -1 is no limit, as everywhere in the SDK; to the system that's 0.
    if (recv_ms == 0 || recv_ms < -1 || send_ms == 0 || send_ms < -1)
        return dino__fail(DINO_EINVAL);
    const int options[2] = { SO_RCVTIMEO, SO_SNDTIMEO };
    const int values[2]  = { recv_ms, send_ms };
    for (int i = 0; i < 2; i++)
    {
        int   ms    = values[i] < 0 ? 0 : values[i];
        DWORD limit = (DWORD)ms;
        if (setsockopt((SOCKET)sock, SOL_SOCKET, options[i], (const char *)&limit, sizeof(limit))
            == SOCKET_ERROR)
            return dino__fail_os();
    }
    return 0;
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
