// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <windows.h>
#endif

#include <stdint.h>
#include <utest.h>

#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/time.h>

// Each test file has its own port range: ctest runs test executables in parallel.
#define PORT 18300

struct pair
{
    dino_socket_t server, client, peer;
};

// peer is the accepted end and is non-blocking; client writes into it.
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
    if (p->peer == DINO_INVALID_SOCKET)
        return -1;
    return dino_socket_set_nonblocking(p->peer, 1);
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

struct event
{
    dino_event_loop_t loop;
    struct pair       p;
};

UTEST_F_SETUP(event)
{
    utest_fixture->loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, utest_fixture->loop);
    ASSERT_EQ(0, open_pair(&utest_fixture->p, PORT));
}

UTEST_F_TEARDOWN(event)
{
    (void)utest_result;
    dino_event_loop_destroy(utest_fixture->loop);
    close_pair(&utest_fixture->p);
}

UTEST(event, create_destroy)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_loop_destroy(loop);
}

UTEST_F(event, read_ready)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(
            utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, &utest_fixture->p));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 0));

    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    EXPECT_FALSE(out[0].events & DINO_EVENT_WRITE);
    EXPECT_EQ((void *)&utest_fixture->p, out[0].userdata);
}

UTEST_F(event, write_ready)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_WRITE, NULL));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_WRITE);
    EXPECT_FALSE(out[0].events & DINO_EVENT_READ);
}

UTEST_F(event, level_rereports)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
        EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    }

    char c;
    ASSERT_EQ(1, dino_socket_recv(utest_fixture->p.peer, &c, 1));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 100));
}

UTEST_F(event, oneshot_rearm)
{
    dino_event_t out[8];
    uint32_t     interest = DINO_EVENT_READ | DINO_EVENT_ONESHOT;
    ASSERT_EQ(0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, interest, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 100));

    ASSERT_EQ(0, dino_event_mod_socket(utest_fixture->loop, utest_fixture->p.peer, interest, NULL));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
}

UTEST_F(event, mod_changes_interest_and_userdata)
{
    dino_event_t out[8];
    int          a, b;
    ASSERT_EQ(
        0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, &a));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 0));

    ASSERT_EQ(
        0, dino_event_mod_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_WRITE, &b));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_WRITE);
    EXPECT_FALSE(out[0].events & DINO_EVENT_READ);
    EXPECT_EQ((void *)&b, out[0].userdata);
}

UTEST_F(event, hangup_when_the_peer_closes)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    dino_socket_close(utest_fixture->p.client);
    utest_fixture->p.client = DINO_INVALID_SOCKET;

    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);
}

UTEST_F(event, del_stops_events)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));

    ASSERT_EQ(0, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 100));
    // And it can be added again.
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
}

UTEST_F(event, del_with_a_poll_in_flight_then_destroy)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 0));
    ASSERT_EQ(0, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    // The fixture teardown destroys the loop with the cancelled poll unreaped.
}

UTEST_F(event, small_out_array_keeps_the_rest)
{
    struct pair q;
    ASSERT_EQ(0, open_pair(&q, PORT + 1));
    int          a, b;
    dino_event_t out[1];
    ASSERT_EQ(
        0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, &a));
    ASSERT_EQ(0,
        dino_event_add_socket(
            utest_fixture->loop, q.peer, DINO_EVENT_READ | DINO_EVENT_ONESHOT, &b));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));
    ASSERT_EQ(1, dino_socket_send(q.client, "y", 1));
    dino_sleep_ms(50);

    // b is oneshot, so b can't be reported twice; a is level-triggered and
    // reported until read. Within three waits both must have come out.
    int seen_a = 0, seen_b = 0;
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 1, 1000));
        seen_a += out[0].userdata == &a;
        seen_b += out[0].userdata == &b;
    }
    EXPECT_GE(seen_a, 1);
    EXPECT_EQ(1, seen_b);

    dino_event_del_socket(utest_fixture->loop, q.peer);
    close_pair(&q);
}

UTEST_F(event, invalid_args)
{
    dino_event_t out[1];
    EXPECT_EQ(-1, dino_event_wait(DINO_INVALID_EVENT_LOOP, out, 1, 0));
    EXPECT_EQ(-1, dino_event_wait(utest_fixture->loop, out, 0, 0));
    EXPECT_EQ(-1, dino_event_wait(utest_fixture->loop, NULL, 1, 0));
    EXPECT_EQ(-1,
        dino_event_add_socket(
            DINO_INVALID_EVENT_LOOP, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(
        -1, dino_event_add_socket(utest_fixture->loop, DINO_INVALID_SOCKET, DINO_EVENT_READ, NULL));

    EXPECT_EQ(-1,
        dino_event_mod_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    dino_event_loop_destroy(DINO_INVALID_EVENT_LOOP);
}

UTEST(event, many_sockets)
{
    enum
    {
        N = 200
    };
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    static dino_socket_t socks[N];
    for (int i = 0; i < N; i++)
    {
        socks[i] = dino_socket_create(DINO_AF_INET, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
        ASSERT_NE(DINO_INVALID_SOCKET, socks[i]);
        ASSERT_EQ(0, dino_event_add_socket(loop, socks[i], DINO_EVENT_WRITE, &socks[i]));
    }
    // Every socket is writable; drain them all in batches with oneshot-like
    // removal, checking each one shows up.
    static int   seen[N];
    dino_event_t out[64];
    int          total = 0;
    for (int round = 0; round < 50 && total < N; round++)
    {
        int n = dino_event_wait(loop, out, 64, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
        {
            int idx = (int)((dino_socket_t *)out[i].userdata - socks);
            ASSERT_TRUE(idx >= 0 && idx < N);
            if (!seen[idx]++)
                total++;
            ASSERT_EQ(0, dino_event_del_socket(loop, socks[idx]));
        }
    }
    EXPECT_EQ(N, total);
    for (int i = 0; i < N; i++)
        dino_socket_close(socks[i]);
    dino_event_loop_destroy(loop);
}

UTEST_MAIN()
