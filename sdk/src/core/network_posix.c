#include <dino/core/network.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
    return sent < 0 ? dino__fail_os() : sent;
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
    return received < 0 ? dino__fail_os() : received;
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
