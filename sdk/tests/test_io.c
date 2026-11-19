#include <utest.h>

#include <dino/core/io.h>
#include <dino/core/network.h>

#define PORT       18200
#define TIMEOUT_MS 5000
#define IDLE_MS    50

struct pair
{
    dino_socket_t server, client, peer;
};

static int open_pair(struct pair *p, int port)
{
    p->server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    p->client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    p->peer   = DINO_INVALID_SOCKET;
    if (p->server == DINO_INVALID_SOCKET || p->client == DINO_INVALID_SOCKET)
        return -1;
    dino_socket_set_reuseaddr(p->server, 1);
    if (dino_socket_bind(p->server, "127.0.0.1", port) != 0
        || dino_socket_listen(p->server, 1) != 0)
        return -1;
    if (dino_socket_connect(p->client, "127.0.0.1", port) != 0)
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

UTEST(poll, idle_socket_times_out)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT));
    dino_pollfd_t pfd = { p.peer, DINO_POLLIN, DINO_POLLERR };
    EXPECT_EQ(0, dino_poll(&pfd, 1, IDLE_MS));
    // revents is reset on timeout, not left from before.
    EXPECT_EQ(0, pfd.revents);
    close_pair(&p);
}

UTEST(poll, pending_connection_is_readable)
{
    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(server, 1);
    ASSERT_EQ(0, dino_socket_bind(server, "127.0.0.1", PORT + 1));
    ASSERT_EQ(0, dino_socket_listen(server, 1));
    ASSERT_EQ(0, dino_socket_connect(client, "127.0.0.1", PORT + 1));

    dino_pollfd_t pfd = { server, DINO_POLLIN, 0 };
    EXPECT_EQ(1, dino_poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & DINO_POLLIN);

    dino_socket_t peer = dino_socket_accept(server);
    EXPECT_NE(DINO_INVALID_SOCKET, peer);
    dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
}

UTEST(poll, data_is_readable)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT + 2));
    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4));
    dino_pollfd_t pfd = { p.peer, DINO_POLLIN, 0 };
    EXPECT_EQ(1, dino_poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & DINO_POLLIN);
    close_pair(&p);
}

UTEST(poll, connected_socket_is_writable)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT + 3));
    dino_pollfd_t pfd = { p.client, DINO_POLLOUT, 0 };
    EXPECT_EQ(1, dino_poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & DINO_POLLOUT);
    close_pair(&p);
}

UTEST(poll, peer_hangup_is_reported)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT + 4));
    dino_socket_close(p.client);
    p.client = DINO_INVALID_SOCKET;

    // Linux reports a closed peer as readable, Windows as DINO_POLLHUP.
    dino_pollfd_t pfd = { p.peer, DINO_POLLIN, 0 };
    EXPECT_EQ(1, dino_poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & (DINO_POLLIN | DINO_POLLHUP));
    char buf[1];
    EXPECT_EQ(0, dino_socket_recv(p.peer, buf, sizeof(buf)));
    close_pair(&p);
}

UTEST(poll, several_sockets_report_individually)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT + 5));
    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4));
    dino_pollfd_t pfds[2] = { { p.peer, DINO_POLLIN, 0 }, { p.server, DINO_POLLIN, 0 } };
    EXPECT_EQ(1, dino_poll(pfds, 2, TIMEOUT_MS));
    EXPECT_TRUE(pfds[0].revents & DINO_POLLIN);
    EXPECT_EQ(0, pfds[1].revents);
    close_pair(&p);
}

UTEST(fd_set, add_test_remove)
{
    dino_socket_t s   = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_fd_set_t set = dino_fd_set_create();
    ASSERT_NE(DINO_INVALID_FD_SET, set);

    EXPECT_FALSE(dino_fd_set_has(set, s));
    dino_fd_set_add(set, s);
    EXPECT_TRUE(dino_fd_set_has(set, s));
    dino_fd_set_del(set, s);
    EXPECT_FALSE(dino_fd_set_has(set, s));
    dino_fd_set_add(set, s);
    dino_fd_set_zero(set);
    EXPECT_FALSE(dino_fd_set_has(set, s));

    dino_fd_set_destroy(set);
    dino_socket_close(s);
}

// Sets are never passed to select here, so made-up socket values are fine.
UTEST(fd_set, sockets_that_dont_fit_are_refused)
{
    dino_fd_set_t set   = dino_fd_set_create();
    dino_fd_set_t other = dino_fd_set_create();
    ASSERT_NE(DINO_INVALID_FD_SET, set);
    ASSERT_NE(DINO_INVALID_FD_SET, other);

    EXPECT_EQ(-1, dino_fd_set_add(set, DINO_INVALID_SOCKET));
    EXPECT_FALSE(dino_fd_set_has(set, DINO_INVALID_SOCKET));
    dino_fd_set_del(set, DINO_INVALID_SOCKET);
#ifdef _WIN32
    // At most 64 sockets, whatever their values.
    for (dino_socket_t s = 0; s < 64; s++)
        ASSERT_EQ(0, dino_fd_set_add(set, 1000 + s * 4));
    EXPECT_EQ(0, dino_fd_set_add(set, 1000));
    EXPECT_EQ(-1, dino_fd_set_add(set, 4));
    EXPECT_FALSE(dino_fd_set_has(set, 4));
#else
    // Descriptors below 1024 only: the set is a 1024-bit array.
    EXPECT_EQ(0, dino_fd_set_add(set, 1023));
    EXPECT_TRUE(dino_fd_set_has(set, 1023));
    for (dino_socket_t s = 1024; s < 4096; s++)
    {
        EXPECT_EQ(-1, dino_fd_set_add(set, s));
        EXPECT_FALSE(dino_fd_set_has(set, s));
        dino_fd_set_del(set, s);
    }
    for (dino_socket_t s = 0; s < 1024; s++)
        EXPECT_FALSE(dino_fd_set_has(other, s));
#endif
    dino_fd_set_destroy(set);
    dino_fd_set_destroy(other);
}

UTEST(fd_set, invalid_set_is_harmless)
{
    EXPECT_EQ(-1, dino_fd_set_add(DINO_INVALID_FD_SET, 0));
    dino_fd_set_del(DINO_INVALID_FD_SET, 0);
    dino_fd_set_zero(DINO_INVALID_FD_SET);
    dino_fd_set_destroy(DINO_INVALID_FD_SET);
    EXPECT_FALSE(dino_fd_set_has(DINO_INVALID_FD_SET, 0));
}

UTEST(select, times_out_then_sees_data)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p, PORT + 6));
    dino_fd_set_t rfds = dino_fd_set_create();
    ASSERT_NE(DINO_INVALID_FD_SET, rfds);

    dino_fd_set_add(rfds, p.peer);
    EXPECT_EQ(0, dino_select(p.peer + 1, rfds, DINO_INVALID_FD_SET, DINO_INVALID_FD_SET, IDLE_MS));

    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4));
    dino_fd_set_zero(rfds);
    dino_fd_set_add(rfds, p.peer);
    EXPECT_EQ(
        1, dino_select(p.peer + 1, rfds, DINO_INVALID_FD_SET, DINO_INVALID_FD_SET, TIMEOUT_MS));
    EXPECT_TRUE(dino_fd_set_has(rfds, p.peer));

    dino_fd_set_destroy(rfds);
    close_pair(&p);
}

UTEST_MAIN()
