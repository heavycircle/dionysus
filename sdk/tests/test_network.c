// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#endif

#include <string.h>
#include <utest.h>

#include <dino/core/network.h>
#include <dino/core/thread.h>
#include <dino/core/time.h>

// Each test file has its own port range: ctest runs test executables in parallel.
#define PORT 18100

struct pair
{
    dino_socket_t server, client, peer;
};

// Listener, connected client and the accepted peer, all in one thread:
// connect() completes against the listen backlog before accept().
static int open_pair(struct pair *p, dino_socket_domain_t domain, const char *ip, int port)
{
    p->server = dino_socket_create(domain, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    p->client = dino_socket_create(domain, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    p->peer   = DINO_INVALID_SOCKET;
    if (p->server == DINO_INVALID_SOCKET || p->client == DINO_INVALID_SOCKET)
        return -1;
    dino_socket_set_reuseaddr(p->server, 1);
    if (dino_socket_bind(p->server, ip, port) != 0 || dino_socket_listen(p->server, 1) != 0)
        return -1;
    if (dino_socket_connect(p->client, ip, port) != 0)
        return -1;
    p->peer = dino_socket_accept(p->server);
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

static void echo(int *utest_result, dino_socket_domain_t domain, const char *ip, int port)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, domain, ip, port));

    char buf[8] = { 0 };
    EXPECT_EQ(4, dino_socket_send(p.client, "ping", 4));
    EXPECT_EQ(4, dino_socket_recv(p.peer, buf, 4));
    EXPECT_STREQ("ping", buf);
    EXPECT_EQ(4, dino_socket_send(p.peer, "pong", 4));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(4, dino_socket_recv(p.client, buf, 4));
    EXPECT_STREQ("pong", buf);

    close_pair(&p);
}

UTEST(network, create_and_close)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_close(s));
}

UTEST(network, close_invalid_socket_fails)
{
    EXPECT_NE(0, dino_socket_close(DINO_INVALID_SOCKET));
}

UTEST(network, udp_socket)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_bind(s, "127.0.0.1", 0));
    EXPECT_EQ(0, dino_socket_close(s));
}

UTEST(network, echo_ipv4)
{
    echo(utest_result, DINO_AF_INET, "127.0.0.1", PORT);
}

UTEST(network, echo_ipv6)
{
    echo(utest_result, DINO_AF_INET6, "::1", PORT + 1);
}

// Linux defaults to dual-stack, Windows to v6-only: the SDK picks v6-only.
UTEST(network, ipv6_sockets_are_v6_only_by_default)
{
    dino_socket_t v6 = dino_socket_create(DINO_AF_INET6, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t v4 = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t c  = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(v6, 1);
    ASSERT_EQ(0, dino_socket_bind(v6, "::", PORT + 9));
    ASSERT_EQ(0, dino_socket_listen(v6, 1));
    // The IPv4 side of the port is still free, and nothing listens on it yet.
    EXPECT_NE(0, dino_socket_connect(c, "127.0.0.1", PORT + 9));
    EXPECT_EQ(0, dino_socket_bind(v4, "0.0.0.0", PORT + 9));
    dino_socket_close(c);
    dino_socket_close(v4);
    dino_socket_close(v6);
}

UTEST(network, set_v6only_0_accepts_ipv4_peers)
{
    dino_socket_t v6 = dino_socket_create(DINO_AF_INET6, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t c  = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(v6, 1);
    ASSERT_EQ(0, dino_socket_set_v6only(v6, 0));
    ASSERT_EQ(0, dino_socket_bind(v6, "::", PORT + 10));
    ASSERT_EQ(0, dino_socket_listen(v6, 1));
    ASSERT_EQ(0, dino_socket_connect(c, "127.0.0.1", PORT + 10));
    dino_socket_t peer = dino_socket_accept(v6);
    EXPECT_NE(DINO_INVALID_SOCKET, peer);
    EXPECT_EQ(2, dino_socket_send(c, "hi", 2));
    char buf[2];
    EXPECT_EQ(2, dino_socket_recv(peer, buf, 2));
    dino_socket_close(peer);
    dino_socket_close(c);
    dino_socket_close(v6);
}

UTEST(network, set_v6only_needs_an_ipv6_socket)
{
    dino_socket_t v4 = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    EXPECT_EQ(-1, dino_socket_set_v6only(v4, 1));
    EXPECT_EQ(-1, dino_socket_set_v6only(DINO_INVALID_SOCKET, 1));
    dino_socket_close(v4);
}

UTEST(network, invalid_addresses_are_rejected)
{
    const char *bad[] = { "localhost", "256.0.0.1", "::zz", "", NULL };
    for (int i = 0; i < 5; i++)
    {
        dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
        ASSERT_NE(DINO_INVALID_SOCKET, s);
        // Port 0 binds anywhere, so only rejecting the string can make this fail.
        EXPECT_NE(0, dino_socket_bind(s, bad[i], 0));
        EXPECT_NE(0, dino_socket_connect(s, bad[i], PORT + 2));
        dino_socket_close(s);
    }
}

UTEST(network, accept_on_unlistened_socket_fails)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(s));
    dino_socket_close(s);
}

UTEST(network, nonblocking_accept_returns_immediately)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    dino_socket_set_reuseaddr(s, 1);
    ASSERT_EQ(0, dino_socket_bind(s, "127.0.0.1", PORT + 3));
    ASSERT_EQ(0, dino_socket_listen(s, 1));
    EXPECT_EQ(0, dino_socket_set_nonblocking(s, 1));
    // Nobody is connecting: a blocking accept would hang here.
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(s));
    dino_socket_close(s);
}

static int send_later(void *arg)
{
    dino_sleep_ms(200);
    return dino_socket_send(*(dino_socket_t *)arg, "x", 1) == 1 ? 0 : -1;
}

// Windows' accept copies the listener's non-blocking mode; Linux's doesn't.
UTEST(network, accepted_sockets_are_blocking)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, DINO_AF_INET, "127.0.0.1", PORT + 8));
    dino_socket_close(p.peer);
    dino_socket_close(p.client);
    ASSERT_EQ(0, dino_socket_set_nonblocking(p.server, 1));
    p.client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    ASSERT_EQ(0, dino_socket_connect(p.client, "127.0.0.1", PORT + 8));
    p.peer = dino_socket_accept(p.server);
    ASSERT_NE(DINO_INVALID_SOCKET, p.peer);

    // Non-blocking, the recv would fail at once instead of waiting for the byte.
    dino_thread_t sender;
    ASSERT_EQ(0, dino_thread_create(&sender, send_later, &p.client));
    char buf[1];
    EXPECT_EQ(1, dino_socket_recv(p.peer, buf, 1));
    int result = -1;
    dino_thread_join(sender, &result);
    EXPECT_EQ(0, result);
    close_pair(&p);
}

UTEST(network, recv_sees_orderly_shutdown)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, DINO_AF_INET, "127.0.0.1", PORT + 4));
    EXPECT_EQ(0, dino_socket_shutdown(p.client, DINO_SHUT_WR));
    char buf[4];
    EXPECT_EQ(0, dino_socket_recv(p.peer, buf, sizeof(buf)));
    close_pair(&p);
}

// A negative len became SIZE_MAX on Linux, and recv wrote past the buffer.
UTEST(network, negative_lengths_are_rejected)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, DINO_AF_INET, "127.0.0.1", PORT + 7));
    ASSERT_EQ(8, dino_socket_send(p.client, "abcdefgh", 8));
    struct
    {
        char buf[4];
        char guard[4];
    } g;
    memset(&g, '.', sizeof(g));
    EXPECT_EQ(-1, dino_socket_recv(p.peer, g.buf, -1));
    EXPECT_EQ(0, memcmp(g.guard, "....", 4));
    EXPECT_EQ(-1, dino_socket_send(p.client, "x", -1));
    EXPECT_EQ(-1, dino_socket_recv(p.peer, NULL, 4));
    EXPECT_EQ(-1, dino_socket_send(p.client, NULL, 4));
    EXPECT_EQ(4, dino_socket_recv(p.peer, g.buf, 4));
    close_pair(&p);
}

// The peer is gone: sends must fail instead of raising SIGPIPE, which would
// end this test process on Linux (#82). The first sends can still succeed
// into the socket buffer until the peer's reset arrives.
UTEST(network, send_to_a_closed_peer_fails_without_killing_the_process)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, DINO_AF_INET, "127.0.0.1", PORT + 6));
    dino_socket_close(p.peer);
    p.peer = DINO_INVALID_SOCKET;

    int rc = 0;
    for (int i = 0; i < 100 && rc >= 0; i++)
    {
        rc = dino_socket_send(p.client, "data", 4);
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
    ASSERT_EQ(0, open_pair(&p, DINO_AF_INET, "127.0.0.1", PORT + 5));
    EXPECT_EQ(0, inheritable(p.server));
    EXPECT_EQ(0, inheritable(p.client));
    EXPECT_EQ(0, inheritable(p.peer));
    close_pair(&p);

    dino_socket_t udp = dino_socket_create(DINO_AF_INET6, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
    ASSERT_NE(DINO_INVALID_SOCKET, udp);
    EXPECT_EQ(0, inheritable(udp));
    dino_socket_close(udp);
}

UTEST_MAIN()
