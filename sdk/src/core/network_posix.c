#include <dino/core/network.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <dino/core/time.h>

#include "error_internal.h"

int dino_net_init(void)
{
    return 0;
}

dino_socket_t dino_socket_create(int domain, int type, int protocol)
{
    // Never inherited by child processes: a child holding a copy keeps a
    // listening port bound, or a connection open, after the parent closes it.
    int s = socket(domain, type | SOCK_CLOEXEC, protocol);
    if (s < 0)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    // The OS default differs (dual-stack on Linux, v6-only on Windows).
    int on = 1;
    if (domain == AF_INET6 && setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0)
    {
        dino__error_set_os();
        close(s);
        return DINO_INVALID_SOCKET;
    }
    return (dino_socket_t)s;
}

int dino_socket_close(dino_socket_t sock)
{
    if (sock == DINO_INVALID_SOCKET)
        return dino__fail(DINO_EINVAL);
    // The descriptor is released even when close reports an error.
    return close((int)sock) < 0 ? dino__fail_os() : 0;
}

dino_socket_t dino_socket_accept(dino_socket_t sock, struct sockaddr *addr, socklen_t *len)
{
    if (addr && !len)
    {
        dino__error_set(DINO_EINVAL);
        return DINO_INVALID_SOCKET;
    }
    // A signal handler interrupts the wait unless it was installed with
    // SA_RESTART, and always when a socket timeout is set. Windows has no
    // signals: retry, as the pipe calls do.
    int client;
    do
        client = accept4((int)sock, addr, addr ? len : NULL, SOCK_CLOEXEC);
    while (client < 0 && errno == EINTR);
    if (client < 0)
    {
        dino__error_set_os();
        return DINO_INVALID_SOCKET;
    }
    return (dino_socket_t)client;
}

int dino_socket_connect(dino_socket_t sock, const struct sockaddr *addr, socklen_t len)
{
    if (!addr)
        return dino__fail(DINO_EINVAL);
    if (connect((int)sock, addr, len) == 0)
        return 0;
    // An interrupted connect isn't undone: it goes on in the background and
    // ends as a non-blocking one does (dino_socket_error once writable).
    // Calling connect again would fail with EALREADY.
    if (errno == EINTR)
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
    if (connect((int)sock, addr, len) != 0)
    {
        if (errno != EINPROGRESS && errno != EINTR)
        {
            rc = dino__fail_os();
        }
        else
        {
            uint64_t      deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
            struct pollfd pfd      = { (int)sock, POLLOUT, 0 };
            int           ready;
            for (;;)
            {
                int wait = timeout_ms;
                if (timeout_ms > 0)
                {
                    uint64_t now = dino_time_monotonic_ns();
                    wait         = now >= deadline ? 0 : (int)((deadline - now + 999999) / 1000000);
                }
                ready = poll(&pfd, 1, wait);
                if (ready >= 0 || errno != EINTR)
                    break;
            }
            if (ready < 0)
                rc = dino__fail_os();
            else if (ready == 0)
                rc = DINO_TIMEOUT;
            else
            {
                int code = dino_socket_error(sock);
                rc       = code ? dino__fail(code) : 0;
            }
        }
    }
    // Blocking again, keeping the reason a failure was reported.
    int code = dino_error();
    dino_socket_set_nonblocking(sock, 0);
    dino__error_set(code);
    return rc;
}

// A receive or send that ran out of SO_RCVTIMEO/SO_SNDTIMEO says EAGAIN on
// Linux, as a non-blocking socket with nothing to do does; Windows says
// WSAETIMEDOUT. Told apart here, on the failure path only.
static int fail_timed_out(dino_socket_t sock, int flags)
{
    if ((errno == EAGAIN || errno == EWOULDBLOCK) && !(flags & MSG_DONTWAIT))
    {
        int mode = fcntl((int)sock, F_GETFL, 0);
        if (mode != -1 && !(mode & O_NONBLOCK))
            return dino__fail(DINO_ETIMEDOUT);
    }
    return dino__fail_os();
}

int dino_socket_set_timeouts(dino_socket_t sock, int recv_ms, int send_ms)
{
    if (recv_ms < -1 || send_ms < -1)
        return dino__fail(DINO_EINVAL);
    const int options[2] = { SO_RCVTIMEO, SO_SNDTIMEO };
    const int values[2]  = { recv_ms, send_ms };
    for (int i = 0; i < 2; i++)
    {
        if (values[i] < 0)
            continue;
        struct timeval tv = { (time_t)(values[i] / 1000), (suseconds_t)(values[i] % 1000) * 1000 };
        if (setsockopt((int)sock, SOL_SOCKET, options[i], &tv, sizeof(tv)) != 0)
            return dino__fail_os();
    }
    return 0;
}

int dino_socket_send(dino_socket_t sock, const void *buf, size_t len, int flags)
{
    if (len > 0 && !buf)
        return dino__fail(DINO_EINVAL);
    // MSG_NOSIGNAL: a closed peer fails the call with EPIPE, as on Windows,
    // instead of raising SIGPIPE, which would end the process. send() never
    // returns more than len, so the result fits in an int.
    int sent;
    do
        sent = (int)send((int)sock, buf, (size_t)dino__clamp_len(len), flags | MSG_NOSIGNAL);
    while (sent < 0 && errno == EINTR);
    return sent < 0 ? fail_timed_out(sock, flags) : sent;
}

int dino_socket_recvfrom(
    dino_socket_t sock, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    if ((len > 0 && !buf) || (from && !fromlen))
        return dino__fail(DINO_EINVAL);
    int received;
    do
        received = (int)recvfrom(
            (int)sock, buf, (size_t)dino__clamp_len(len), flags, from, from ? fromlen : NULL);
    while (received < 0 && errno == EINTR);
    return received < 0 ? fail_timed_out(sock, flags) : received;
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
    return setsockopt((int)sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) != 0
        ? dino__fail_os()
        : 0;
}

int dino_socket_set_nonblocking(dino_socket_t sock, int enable)
{
    int flags = fcntl((int)sock, F_GETFL, 0);
    if (flags == -1)
        return dino__fail_os();
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl((int)sock, F_SETFL, flags) == -1 ? dino__fail_os() : 0;
}

int dino_socket_error(dino_socket_t sock)
{
    int       pending = 0;
    socklen_t len     = sizeof(pending);
    if (getsockopt((int)sock, SOL_SOCKET, SO_ERROR, &pending, &len) != 0)
        return dino__error_from_os();
    return pending ? dino__error_from_code(pending) : 0;
}

int dino_socket_last_error(void)
{
    return dino__error_from_os();
}
