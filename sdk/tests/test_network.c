// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <winsock2.h>

#include <windows.h>
#else
#include <fcntl.h>
#endif

#include <netdb.h>
#include <netinet/tcp.h>
#include <string.h>
#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/network.h>
#include <dino/core/time.h>

#include "../src/core/thread_internal.h"
#include "net_helpers.h"

struct pair
{
    dino_socket_t server, client, peer;
};

// Listener, connected client and the accepted peer, all in one thread:
// connect() completes against the listen backlog before accept().
static int open_pair(struct pair *p, int domain, const char *ip)
{
    p->server = bound_socket(domain, SOCK_STREAM, ip, 1);
    p->client = dino_socket_create(domain, SOCK_STREAM, 0);
    p->peer   = DINO_INVALID_SOCKET;
    if (p->server == DINO_INVALID_SOCKET || p->client == DINO_INVALID_SOCKET)
        return -1;
    if (connect_to(p->client, ip, local_port(p->server)) != 0)
        return -1;
    p->peer = dino_socket_accept(p->server, NULL, NULL);
    return p->peer == DINO_INVALID_SOCKET ? -1 : 0;
}

static void close_pair(struct pair *p)
{
    if (p->peer != DINO_INVALID_SOCKET)
        dino_socket_close(p->peer);
    if (p->client != DINO_INVALID_SOCKET)
        dino_socket_close(p->client);
    if (p->server != DINO_INVALID_SOCKET)
        dino_socket_close(p->server);
}

static void echo(int *utest_result, int domain, const char *ip)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, domain, ip));

    char buf[8] = { 0 };
    EXPECT_EQ(4, dino_socket_send(p.client, "ping", 4, 0));
    EXPECT_EQ(4, recv_some(p.peer, buf, 4));
    EXPECT_STREQ("ping", buf);
    EXPECT_EQ(4, dino_socket_send(p.peer, "pong", 4, 0));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, dino_socket_recvfrom(p.client, buf, 4, 0, NULL, NULL));
    EXPECT_STREQ("pong", buf);

    close_pair(&p);
}

UTEST(network, create_and_close)
{
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_close(s));
}

// Protocol 0 picks the type's usual protocol, as socket(2) does.
UTEST(network, create_takes_the_platform_constants)
{
    dino_socket_t s = dino_socket_create(AF_INET6, SOCK_DGRAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_close(s));
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_create(-1, SOCK_STREAM, 0));
    EXPECT_NE(0, dino_error()); // which code depends on the platform
}

UTEST(network, close_invalid_socket_fails)
{
    EXPECT_EQ(-1, dino_socket_close(DINO_INVALID_SOCKET));
}

UTEST(network, echo_ipv4)
{
    echo(utest_result, AF_INET, "127.0.0.1");
}

UTEST(network, echo_ipv6)
{
    SKIP_WITHOUT_IPV6();
    echo(utest_result, AF_INET6, "::1");
}

// Linux defaults to dual-stack, Windows to v6-only: the SDK picks v6-only.
UTEST(network, ipv6_sockets_are_v6_only_by_default)
{
    SKIP_WITHOUT_IPV6();
    dino_socket_t v6 = bound_socket(AF_INET6, SOCK_STREAM, "::", 1);
    dino_socket_t v4 = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_t c  = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, v6);
    int port = local_port(v6);
    // The IPv4 side of the port is still free, and nothing listens on it yet.
    EXPECT_EQ(-1, connect_to(c, "127.0.0.1", port));
    EXPECT_EQ(0, bind_to(v4, "0.0.0.0", port));
    dino_socket_close(c);
    dino_socket_close(v4);
    dino_socket_close(v6);
}

// IPV6_V6ONLY through plain setsockopt makes a dual-stack listener.
UTEST(network, clearing_v6only_accepts_ipv4_peers)
{
    SKIP_WITHOUT_IPV6();
    dino_socket_t v6 = dino_socket_create(AF_INET6, SOCK_STREAM, 0);
    dino_socket_t c  = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, set_int_option(v6, IPPROTO_IPV6, IPV6_V6ONLY, 0));
    ASSERT_EQ(0, bind_to(v6, "::", 0));
    ASSERT_EQ(0, listen(v6, 1));
    ASSERT_EQ(0, connect_to(c, "127.0.0.1", local_port(v6)));
    dino_socket_t peer = dino_socket_accept(v6, NULL, NULL);
    EXPECT_NE(DINO_INVALID_SOCKET, peer);
    EXPECT_EQ(2, dino_socket_send(c, "hi", 2, 0));
    char buf[2];
    EXPECT_EQ(2, recv_some(peer, buf, 2));
    dino_socket_close(peer);
    dino_socket_close(c);
    dino_socket_close(v6);
}

// Rebinding after a restart is the point; taking a port someone is listening
// on is not, though Windows' SO_REUSEADDR would allow it (wine doesn't).
UTEST(network, reuseaddr_never_shares_a_listening_port)
{
    dino_socket_t a = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_t b = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    EXPECT_EQ(0, dino_socket_set_reuseaddr(a, 1));
    EXPECT_EQ(0, dino_socket_set_reuseaddr(b, 1));
    ASSERT_EQ(0, bind_to(a, "127.0.0.1", 0));
    ASSERT_EQ(0, listen(a, 1));
    EXPECT_EQ(-1, bind_to(b, "127.0.0.1", local_port(a)));
    EXPECT_EQ(DINO_EADDRINUSE, dino_socket_last_error());
    EXPECT_EQ(0, dino_socket_set_reuseaddr(a, 0));
    EXPECT_EQ(-1, dino_socket_set_reuseaddr(DINO_INVALID_SOCKET, 1));
    dino_socket_close(b);
    dino_socket_close(a);
}

// Linux's SO_REUSEADDR lets a second datagram socket bind the same port and
// take the first one's datagrams; Windows never allows it through this call.
// On a datagram socket it does nothing, on both.
UTEST(network, reuseaddr_never_shares_a_udp_port)
{
    dino_socket_t a = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    dino_socket_t b = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, a);
    ASSERT_NE(DINO_INVALID_SOCKET, b);
    EXPECT_EQ(0, dino_socket_set_reuseaddr(a, 1));
    EXPECT_EQ(0, dino_socket_set_reuseaddr(b, 1));
    ASSERT_EQ(0, bind_to(a, "127.0.0.1", 0));
    EXPECT_EQ(-1, bind_to(b, "127.0.0.1", local_port(a)));
    dino_socket_close(b);
    dino_socket_close(a);
}

// A UDP server answers whoever sent the datagram, from recvfrom's address.
static void udp_answer(int *utest_result, int domain, const char *ip)
{
    dino_socket_t server = dino_socket_create(domain, SOCK_DGRAM, 0);
    dino_socket_t client = dino_socket_create(domain, SOCK_DGRAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    ASSERT_NE(DINO_INVALID_SOCKET, client);
    ASSERT_EQ(0, bind_to(server, ip, 0));
    ASSERT_EQ(0, bind_to(client, ip, 0));

    ASSERT_EQ(4, sendto_ip(client, "ping", 4, ip, local_port(server)));
    char                    buf[16] = { 0 };
    struct sockaddr_storage from;
    socklen_t               fromlen = sizeof(from);
    ASSERT_EQ(
        4, dino_socket_recvfrom(server, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen));
    EXPECT_STREQ("ping", buf);
    EXPECT_EQ(domain, from.ss_family);

    // Answer to the address recvfrom reported.
    EXPECT_EQ(4, (int)sendto(server, "pong", 4, 0, (struct sockaddr *)&from, fromlen));
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(4, dino_socket_recvfrom(client, buf, sizeof(buf), 0, NULL, NULL));
    EXPECT_STREQ("pong", buf);
    dino_socket_close(client);
    dino_socket_close(server);
}

UTEST(network, udp_server_answers_the_sender_ipv4)
{
    udp_answer(utest_result, AF_INET, "127.0.0.1");
}

UTEST(network, udp_server_answers_the_sender_ipv6)
{
    SKIP_WITHOUT_IPV6();
    udp_answer(utest_result, AF_INET6, "::1");
}

// Windows would fail the call; both platforms return the datagram cut to fit.
UTEST(network, a_datagram_larger_than_the_buffer_is_cut_to_fit)
{
    dino_socket_t a = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    dino_socket_t b = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    ASSERT_EQ(0, bind_to(a, "127.0.0.1", 0));
    int port = local_port(a);
    ASSERT_EQ(10, sendto_ip(b, "0123456789", 10, "127.0.0.1", port));
    char buf[4] = { 0 };
    EXPECT_EQ(4, dino_socket_recvfrom(a, buf, sizeof(buf), 0, NULL, NULL));
    EXPECT_EQ(0, memcmp(buf, "0123", 4));
    dino_socket_close(b);
    dino_socket_close(a);
}

// A datagram to a port nobody listens on doesn't make the next receive fail
// (Windows reports the ICMP error there unless told not to).
UTEST(network, a_datagram_to_a_closed_port_doesnt_fail_the_next_receive)
{
    dino_socket_t gone   = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    dino_socket_t server = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    ASSERT_EQ(0, bind_to(gone, "127.0.0.1", 0));
    ASSERT_EQ(0, bind_to(server, "127.0.0.1", 0));
    int gone_port = local_port(gone), server_port = local_port(server);
    dino_socket_close(gone);

    ASSERT_EQ(1, sendto_ip(server, "x", 1, "127.0.0.1", gone_port));
    dino_sleep_ms(50);
    ASSERT_EQ(1, sendto_ip(client, "y", 1, "127.0.0.1", server_port));
    char buf[4] = { 0 };
    EXPECT_EQ(1, dino_socket_recvfrom(server, buf, sizeof(buf), 0, NULL, NULL));
    EXPECT_EQ('y', buf[0]);
    dino_socket_close(client);
    dino_socket_close(server);
}

// accept reports the peer, which is the client's own local address.
UTEST(network, accept_reports_the_peer_address)
{
    dino_socket_t server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 1);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    ASSERT_EQ(0, connect_to(client, "127.0.0.1", local_port(server)));
    struct sockaddr_in peer;
    socklen_t          len  = sizeof(peer);
    dino_socket_t      conn = dino_socket_accept(server, (struct sockaddr *)&peer, &len);
    ASSERT_NE(DINO_INVALID_SOCKET, conn);
    EXPECT_EQ(AF_INET, peer.sin_family);
    EXPECT_EQ(local_port(client), ntohs(peer.sin_port));
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(server, (struct sockaddr *)&peer, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    dino_socket_close(conn);
    dino_socket_close(client);
    dino_socket_close(server);
}

// The POSIX calls take a dino_socket_t on every target, with the same
// constants: setsockopt, getsockname, shutdown, getaddrinfo.
UTEST(network, posix_calls_work_on_a_dino_socket)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    EXPECT_EQ(0, set_int_option(p.client, IPPROTO_TCP, TCP_NODELAY, 1));
    // getsockname on the listener, getpeername on the client: the same port.
    struct sockaddr_in to;
    socklen_t          to_len = sizeof(to);
    ASSERT_EQ(0, getpeername(p.client, (struct sockaddr *)&to, &to_len));
    EXPECT_EQ(local_port(p.server), ntohs(to.sin_port));
    EXPECT_EQ(0, shutdown(p.client, SHUT_WR));
    char buf[4];
    EXPECT_EQ(0, recv_some(p.peer, buf, sizeof(buf)));
    close_pair(&p);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;
    ASSERT_EQ(0, getaddrinfo("127.0.0.1", "80", &hints, &res));
    EXPECT_EQ(AF_INET, res->ai_family);
    freeaddrinfo(res);
}

// Raw calls report why they failed through dino_socket_last_error, from errno
// on Linux and WSAGetLastError on Windows.
UTEST(network, last_error_reads_a_failed_posix_call)
{
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    char buf[1];
    EXPECT_GT(0, recv_some(s, buf, 1));
    EXPECT_EQ(DINO_ENOTCONN, dino_socket_last_error());
    dino_socket_close(s);
}

UTEST(network, accept_on_unlistened_socket_fails)
{
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(s, NULL, NULL));
    dino_socket_close(s);
}

UTEST(network, nonblocking_accept_returns_immediately)
{
    dino_socket_t s = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 1);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_set_nonblocking(s, 1));
    // Nobody is connecting: a blocking accept would hang here.
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(s, NULL, NULL));
    EXPECT_EQ(DINO_EAGAIN, dino_error());
    dino_socket_close(s);
}

static int send_later(void *arg)
{
    dino_sleep_ms(200);
    return dino_socket_send(*(dino_socket_t *)arg, "x", 1, 0) == 1 ? 0 : -1;
}

// Windows' accept copies the listener's non-blocking mode; Linux's doesn't.
UTEST(network, accepted_sockets_are_blocking)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    dino_socket_close(p.peer);
    dino_socket_close(p.client);
    ASSERT_EQ(0, dino_socket_set_nonblocking(p.server, 1));
    p.client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect_to(p.client, "127.0.0.1", local_port(p.server)));
    p.peer = dino_socket_accept(p.server, NULL, NULL);
    ASSERT_NE(DINO_INVALID_SOCKET, p.peer);

    // Non-blocking, the recv would fail at once instead of waiting for the byte.
    dino__thread_t sender;
    ASSERT_EQ(0, dino__thread_create(&sender, send_later, &p.client));
    char buf[1];
    EXPECT_EQ(1, dino_socket_recvfrom(p.peer, buf, 1, 0, NULL, NULL));
    int result = -1;
    dino__thread_join(sender, &result);
    EXPECT_EQ(0, result);
    close_pair(&p);
}

// Non-blocking, a recv with nothing to read fails at once; switched back, it
// waits again.
UTEST(network, nonblocking_recv_and_back_to_blocking)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    char buf[1];
    ASSERT_EQ(0, dino_socket_set_nonblocking(p.peer, 1));
    EXPECT_EQ(-1, dino_socket_recvfrom(p.peer, buf, 1, 0, NULL, NULL));
    EXPECT_EQ(DINO_EAGAIN, dino_error());

    ASSERT_EQ(0, dino_socket_set_nonblocking(p.peer, 0));
    dino__thread_t sender;
    ASSERT_EQ(0, dino__thread_create(&sender, send_later, &p.client));
    EXPECT_EQ(1, dino_socket_recvfrom(p.peer, buf, 1, 0, NULL, NULL));
    int result = -1;
    dino__thread_join(sender, &result);
    EXPECT_EQ(0, result);
    close_pair(&p);
}

// A length with no buffer behind it is refused before the system sees it.
UTEST(network, null_buffers_are_rejected)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    ASSERT_EQ(8, dino_socket_send(p.client, "abcdefgh", 8, 0));
    EXPECT_EQ(-1, dino_socket_recvfrom(p.peer, NULL, 4, 0, NULL, NULL));
    EXPECT_EQ(-1, dino_socket_send(p.client, NULL, 4, 0));
    struct sockaddr_storage from;
    char                    buf[4];
    EXPECT_EQ(-1, dino_socket_recvfrom(p.peer, buf, 4, 0, (struct sockaddr *)&from, NULL));
    EXPECT_EQ(4, dino_socket_recvfrom(p.peer, buf, sizeof(buf), 0, NULL, NULL));
    close_pair(&p);
}

// The peer is gone: sends must fail instead of raising SIGPIPE, which would
// end this test process on Linux (#82). The first sends can still succeed
// into the socket buffer until the peer's reset arrives.
UTEST(network, send_to_a_closed_peer_fails_without_killing_the_process)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    dino_socket_close(p.peer);
    p.peer = DINO_INVALID_SOCKET;

    int rc = 0;
    for (int i = 0; i < 100 && rc >= 0; i++)
    {
        rc = dino_socket_send(p.client, "data", 4, 0);
        if (rc >= 0)
            dino_sleep_ms(10);
    }
    EXPECT_EQ(-1, rc);
    close_pair(&p);
}

static int inheritable(dino_socket_t s)
{
#ifdef _WIN32
    DWORD flags = 0;
    if (!GetHandleInformation((HANDLE)s, &flags))
        return -1;
    return (flags & HANDLE_FLAG_INHERIT) != 0;
#else
    int flags = fcntl((int)s, F_GETFD);
    if (flags < 0)
        return -1;
    return (flags & FD_CLOEXEC) == 0;
#endif
}

// A child process holding a copy would keep the port bound or the connection
// open after the parent closes it (#77).
UTEST(network, sockets_are_not_inherited_by_children)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, AF_INET, "127.0.0.1"));
    EXPECT_EQ(0, inheritable(p.server));
    EXPECT_EQ(0, inheritable(p.client));
    EXPECT_EQ(0, inheritable(p.peer));
    close_pair(&p);

    dino_socket_t udp = dino_socket_create(AF_INET6, SOCK_DGRAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, udp);
    EXPECT_EQ(0, inheritable(udp));
    dino_socket_close(udp);
}

UTEST(network, init_can_repeat)
{
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(0, dino_net_init());
}

UTEST_MAIN()
