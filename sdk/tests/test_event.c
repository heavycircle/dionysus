// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <windows.h>
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <utest.h>

#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>
#include <dino/core/thread.h>
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

static int wake_later(void *arg)
{
    dino_sleep_ms(50);
    return dino_event_wake(*(dino_event_loop_t *)arg);
}

UTEST(event, wake_from_another_thread)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, wake_later, &loop));

    dino_event_t out[4];
    uint64_t     start = dino_time_monotonic_ns();
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_LT((dino_time_monotonic_ns() - start) / 1000000, 4000u);
    EXPECT_EQ((uint32_t)DINO_EVENT_WAKE, out[0].events);
    EXPECT_EQ(NULL, out[0].userdata);

    int result = -1;
    EXPECT_EQ(0, dino_thread_join(t, &result));
    EXPECT_EQ(0, result);
    dino_event_loop_destroy(loop);
}

UTEST(event, wakes_coalesce)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    for (int i = 0; i < 3; i++)
        ASSERT_EQ(0, dino_event_wake(loop));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 0));
    EXPECT_EQ((uint32_t)DINO_EVENT_WAKE, out[0].events);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));

    // A wake after the last one was reported is reported again.
    ASSERT_EQ(0, dino_event_wake(loop));
    EXPECT_EQ(1, dino_event_wait(loop, out, 4, 0));
    EXPECT_EQ(-1, dino_event_wake(DINO_INVALID_EVENT_LOOP));
    dino_event_loop_destroy(loop);
}

UTEST_F(event, wake_alongside_socket_events)
{
    dino_event_t out[4];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_WRITE, NULL));
    ASSERT_EQ(0, dino_event_wake(utest_fixture->loop));
    dino_sleep_ms(50);

    int seen_wake = 0, seen_write = 0;
    for (int round = 0; round < 3 && !(seen_wake && seen_write); round++)
    {
        int n = dino_event_wait(utest_fixture->loop, out, 4, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
        {
            seen_wake += out[i].events == DINO_EVENT_WAKE;
            seen_write += (out[i].events & DINO_EVENT_WRITE) != 0;
        }
    }
    EXPECT_EQ(1, seen_wake);
    EXPECT_GE(seen_write, 1);
}

static uint64_t elapsed_ms(uint64_t start)
{
    return (dino_time_monotonic_ns() - start) / 1000000;
}

UTEST(event, timer_fires_once)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    int                x     = 0;
    uint64_t           start = dino_time_monotonic_ns();
    dino_event_timer_t timer = dino_event_timer_add(loop, 50, 0, &x);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_GE(elapsed_ms(start), 50u);
    EXPECT_EQ((uint32_t)DINO_EVENT_TIMER, out[0].events);
    EXPECT_EQ((void *)&x, out[0].userdata);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));

    EXPECT_EQ(0, dino_event_timer_del(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, timer_repeats)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_timer_add(loop, 20, 20, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    dino_event_t out[4];
    int          fired = 0;
    uint64_t     start = dino_time_monotonic_ns();
    while (fired < 5 && elapsed_ms(start) < 5000)
    {
        int n = dino_event_wait(loop, out, 4, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
            fired += out[i].events == DINO_EVENT_TIMER;
    }
    EXPECT_EQ(5, fired);
    EXPECT_GE(elapsed_ms(start), 100u);
    EXPECT_EQ(0, dino_event_timer_del(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, timers_fire_in_deadline_order)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    int                ids[3] = { 90, 30, 60 };
    dino_event_timer_t timers[3];
    for (int i = 0; i < 3; i++)
    {
        timers[i] = dino_event_timer_add(loop, (uint32_t)ids[i], 0, &ids[i]);
        ASSERT_NE(DINO_INVALID_EVENT_TIMER, timers[i]);
    }

    int          order[3], got = 0;
    dino_event_t out[4];
    while (got < 3)
    {
        int n = dino_event_wait(loop, out, 4, 5000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n && got < 3; i++)
            order[got++] = *(int *)out[i].userdata;
    }
    EXPECT_EQ(30, order[0]);
    EXPECT_EQ(60, order[1]);
    EXPECT_EQ(90, order[2]);
    for (int i = 0; i < 3; i++)
        EXPECT_EQ(0, dino_event_timer_del(loop, timers[i]));
    dino_event_loop_destroy(loop);
}

UTEST(event, timer_deleted_before_it_fires)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t keep  = dino_event_timer_add(loop, 10000, 0, NULL);
    dino_event_timer_t timer = dino_event_timer_add(loop, 50, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    EXPECT_EQ(0, dino_event_timer_del(loop, timer));

    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 150));
    // keep is never deleted: destroy frees it.
    (void)keep;
    dino_event_loop_destroy(loop);
}

struct blocked_wait
{
    dino_event_loop_t loop;
    dino_event_t      out[4];
    int               n;
};

static int wait_long(void *arg)
{
    struct blocked_wait *w = arg;
    w->n                   = dino_event_wait(w->loop, w->out, 4, 10000);
    return 0;
}

UTEST(event, timer_added_while_waiting_shortens_the_wait)
{
    struct blocked_wait w = { 0 };
    w.loop                = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, w.loop);
    dino_thread_t t;
    ASSERT_EQ(0, dino_thread_create(&t, wait_long, &w));
    dino_sleep_ms(50);

    uint64_t           start = dino_time_monotonic_ns();
    dino_event_timer_t timer = dino_event_timer_add(w.loop, 10, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    EXPECT_EQ(0, dino_thread_join(t, NULL));
    EXPECT_LT(elapsed_ms(start), 5000u);
    EXPECT_EQ(1, w.n);
    EXPECT_EQ((uint32_t)DINO_EVENT_TIMER, w.out[0].events);

    EXPECT_EQ(0, dino_event_timer_del(w.loop, timer));
    dino_event_loop_destroy(w.loop);
}

UTEST_F(event, timer_and_socket_in_one_wait)
{
    dino_event_timer_t timer = dino_event_timer_add(utest_fixture->loop, 0, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_WRITE, NULL));
    dino_sleep_ms(50);

    int          seen_timer = 0, seen_write = 0;
    dino_event_t out[8];
    for (int round = 0; round < 3 && !(seen_timer && seen_write); round++)
    {
        int n = dino_event_wait(utest_fixture->loop, out, 8, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
        {
            seen_timer += out[i].events == DINO_EVENT_TIMER;
            seen_write += (out[i].events & DINO_EVENT_WRITE) != 0;
        }
    }
    EXPECT_EQ(1, seen_timer);
    EXPECT_GE(seen_write, 1);
    EXPECT_EQ(0, dino_event_timer_del(utest_fixture->loop, timer));
}

UTEST(event, timer_invalid_args)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    EXPECT_EQ(DINO_INVALID_EVENT_TIMER, dino_event_timer_add(DINO_INVALID_EVENT_LOOP, 1, 0, NULL));
    EXPECT_EQ(-1, dino_event_timer_del(loop, DINO_INVALID_EVENT_TIMER));
    EXPECT_EQ(-1, dino_event_timer_del(DINO_INVALID_EVENT_LOOP, DINO_INVALID_EVENT_TIMER));
    dino_event_loop_destroy(loop);
}

struct waiter
{
    dino_event_loop_t loop;
    int               timeout_ms;
    dino_event_t      out[4];
    int               n;
    atomic_int        started;
};

static int wait_once(void *arg)
{
    struct waiter *w = arg;
    atomic_store(&w->started, 1);
    w->n = dino_event_wait(w->loop, w->out, 4, w->timeout_ms);
    return 0;
}

static int start_waiter(struct waiter *w, dino_thread_t *t)
{
    atomic_store(&w->started, 0);
    if (dino_thread_create(t, wait_once, w) != 0)
        return -1;
    while (!atomic_load(&w->started))
        dino_thread_yield();
    // Started is set just before the wait; give it time to block.
    dino_sleep_ms(10);
    return 0;
}

// On Windows the deleted socket's poll is still in flight while the other
// thread waits: its cancelled completion must not touch freed state or be
// reported.
UTEST(event, del_and_close_while_another_thread_waits)
{
    struct waiter w = { 0 };
    w.loop          = dino_event_loop_create();
    w.timeout_ms    = 5000;
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, w.loop);
    for (int i = 0; i < 50; i++)
    {
        dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
        ASSERT_NE(DINO_INVALID_SOCKET, s);
        ASSERT_EQ(0, dino_event_add_socket(w.loop, s, DINO_EVENT_READ, &w));

        dino_thread_t t;
        ASSERT_EQ(0, start_waiter(&w, &t));
        ASSERT_EQ(0, dino_event_del_socket(w.loop, s));
        ASSERT_EQ(0, dino_socket_close(s));
        ASSERT_EQ(0, dino_event_wake(w.loop));
        ASSERT_EQ(0, dino_thread_join(t, NULL));

        ASSERT_EQ(1, w.n);
        ASSERT_EQ((uint32_t)DINO_EVENT_WAKE, w.out[0].events);
    }
    dino_event_loop_destroy(w.loop);
}

UTEST(event, mod_while_another_thread_waits)
{
    struct waiter w = { 0 };
    w.loop          = dino_event_loop_create();
    w.timeout_ms    = 5000;
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, w.loop);
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    ASSERT_EQ(0, dino_event_add_socket(w.loop, s, DINO_EVENT_READ, NULL));

    dino_thread_t t;
    ASSERT_EQ(0, start_waiter(&w, &t));
    uint64_t start = dino_time_monotonic_ns();
    ASSERT_EQ(0, dino_event_mod_socket(w.loop, s, DINO_EVENT_WRITE, &w));
    ASSERT_EQ(0, dino_thread_join(t, NULL));
    EXPECT_LT(elapsed_ms(start), 4000u);
    ASSERT_EQ(1, w.n);
    EXPECT_TRUE(w.out[0].events & DINO_EVENT_WRITE);
    EXPECT_EQ((void *)&w, w.out[0].userdata);

    dino_event_del_socket(w.loop, s);
    dino_socket_close(s);
    dino_event_loop_destroy(w.loop);
}

UTEST_F(event, oneshot_goes_to_one_of_several_waiters)
{
    enum
    {
        WAITERS = 4
    };
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer,
            DINO_EVENT_READ | DINO_EVENT_ONESHOT, NULL));
    static struct waiter w[WAITERS];
    dino_thread_t        t[WAITERS];
    for (int i = 0; i < WAITERS; i++)
    {
        w[i].loop       = utest_fixture->loop;
        w[i].timeout_ms = 1500;
        ASSERT_EQ(0, start_waiter(&w[i], &t[i]));
    }
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1));

    int reads = 0;
    for (int i = 0; i < WAITERS; i++)
    {
        ASSERT_EQ(0, dino_thread_join(t[i], NULL));
        ASSERT_GE(w[i].n, 0);
        for (int j = 0; j < w[i].n; j++)
            reads += (w[i].out[j].events & DINO_EVENT_READ) != 0;
    }
    EXPECT_EQ(1, reads);
}

UTEST(event, pipe_read_ready_and_level)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, &r));

    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 0));
    ASSERT_EQ(1, dino_pipe_write(w, "x", 1));
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
        EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
        EXPECT_FALSE(out[0].events & DINO_EVENT_HANGUP);
        EXPECT_EQ((void *)&r, out[0].userdata);
    }
    char c;
    ASSERT_EQ(1, dino_pipe_read(r, &c, 1));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));

    EXPECT_EQ(0, dino_event_del_pipe(loop, r));
    dino_pipe_close(r);
    dino_pipe_close(w);
    dino_event_loop_destroy(loop);
}

UTEST(event, pipe_oneshot_rearm)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    uint32_t interest = DINO_EVENT_READ | DINO_EVENT_ONESHOT;
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, interest, NULL));
    ASSERT_EQ(1, dino_pipe_write(w, "x", 1));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));
    int ud;
    ASSERT_EQ(0, dino_event_mod_pipe(loop, r, interest, &ud));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_EQ((void *)&ud, out[0].userdata);

    dino_event_del_pipe(loop, r);
    dino_pipe_close(r);
    dino_pipe_close(w);
    dino_event_loop_destroy(loop);
}

UTEST(event, pipe_hangup_when_the_writer_closes)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 0));

    ASSERT_EQ(0, dino_pipe_close(w));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    char c;
    EXPECT_EQ(0, dino_pipe_read(r, &c, 1));

    dino_event_del_pipe(loop, r);
    dino_pipe_close(r);
    dino_event_loop_destroy(loop);
}

UTEST(event, pipe_already_closed_when_added)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_pipe_close(w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);

    dino_event_del_pipe(loop, r);
    dino_pipe_close(r);
    dino_event_loop_destroy(loop);
}

UTEST(event, pipe_write_end_and_write_interest_rejected)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(-1, dino_event_add_pipe(loop, w, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_add_pipe(loop, r, DINO_EVENT_WRITE, NULL));
    EXPECT_EQ(-1, dino_event_add_pipe(loop, r, DINO_EVENT_READ | DINO_EVENT_WRITE, NULL));
    EXPECT_EQ(-1, dino_event_add_pipe(loop, DINO_INVALID_PIPE, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_mod_pipe(loop, r, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_del_pipe(loop, r));

    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_mod_pipe(loop, r, DINO_EVENT_WRITE, NULL));

    dino_event_del_pipe(loop, r);
    dino_pipe_close(r);
    dino_pipe_close(w);
    dino_event_loop_destroy(loop);
}

UTEST(event, pipe_del_with_a_read_in_flight_then_readd)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 0));
    ASSERT_EQ(0, dino_event_del_pipe(loop, r));
    ASSERT_EQ(1, dino_pipe_write(w, "x", 1));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));

    // Back into the same loop: it was attached to this loop's port already.
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);

    // Deleted with a read in flight, closed, and the loop destroyed without
    // reaping the cancelled read.
    dino_pipe_t r2, w2;
    ASSERT_EQ(0, dino_pipe_create(&r2, &w2));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r2, DINO_EVENT_READ, NULL));
    ASSERT_EQ(0, dino_event_del_pipe(loop, r2));
    dino_pipe_close(r2);
    dino_pipe_close(w2);

    dino_event_del_pipe(loop, r);
    dino_pipe_close(r);
    dino_pipe_close(w);
    dino_event_loop_destroy(loop);
}

#ifdef _WIN32
// Attaching a handle to a completion port is permanent on Windows.
UTEST(event, pipe_joins_only_one_loop_on_windows)
{
    dino_event_loop_t a = dino_event_loop_create(), b = dino_event_loop_create();
    dino_pipe_t       r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(a, r, DINO_EVENT_READ, NULL));
    ASSERT_EQ(0, dino_event_del_pipe(a, r));
    EXPECT_EQ(-1, dino_event_add_pipe(b, r, DINO_EVENT_READ, NULL));
    dino_pipe_close(r);
    dino_pipe_close(w);
    dino_event_loop_destroy(a);
    dino_event_loop_destroy(b);
}
#endif

UTEST_F(event, pipe_and_socket_in_one_loop)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    int on_pipe, on_sock;
    ASSERT_EQ(0, dino_event_add_pipe(utest_fixture->loop, r, DINO_EVENT_READ, &on_pipe));
    ASSERT_EQ(0,
        dino_event_add_socket(
            utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, &on_sock));
    ASSERT_EQ(1, dino_pipe_write(w, "x", 1));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "y", 1));
    dino_sleep_ms(50);

    int          seen_pipe = 0, seen_sock = 0;
    dino_event_t out[8];
    for (int round = 0; round < 3 && !(seen_pipe && seen_sock); round++)
    {
        int n = dino_event_wait(utest_fixture->loop, out, 8, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
        {
            seen_pipe += out[i].userdata == &on_pipe;
            seen_sock += out[i].userdata == &on_sock;
        }
    }
    EXPECT_GE(seen_pipe, 1);
    EXPECT_GE(seen_sock, 1);

    dino_event_del_pipe(utest_fixture->loop, r);
    dino_pipe_close(r);
    dino_pipe_close(w);
}

#ifdef _WIN32
#define SHELL_ECHO { "cmd.exe", "/d", "/c", "echo hi", NULL }
#else
#define SHELL_ECHO { "/bin/sh", "-c", "echo hi", NULL }
#endif

UTEST(event, child_stdout_through_the_loop)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    char *const               argv[] = SHELL_ECHO;
    dino_process_spawn_opts_t opts   = { 0 };
    dino_process_t            proc;
    dino_process_pipes_t      pipes;
    opts.stdout_mode = DINO_STDIO_PIPE;
    ASSERT_EQ(0, dino_process_spawn(&proc, argv, &opts, &pipes));
    ASSERT_EQ(0, dino_event_add_pipe(loop, pipes.out, DINO_EVENT_READ, NULL));

    char         buf[64];
    int          len = 0, hangup = 0;
    dino_event_t out[4];
    while (!hangup)
    {
        ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
        ASSERT_TRUE(out[0].events & DINO_EVENT_READ);
        int n = dino_pipe_read(pipes.out, buf + len, (int)sizeof(buf) - 1 - len);
        ASSERT_GE(n, 0);
        len += n;
        hangup = n == 0;
    }
    buf[len] = '\0';
    EXPECT_EQ(0, strncmp(buf, "hi", 2));

    dino_event_del_pipe(loop, pipes.out);
    dino_pipe_close(pipes.out);
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(proc, &code));
    EXPECT_EQ(0, code);
    dino_event_loop_destroy(loop);
}

/* ---- processes ---- */

// Linux children are /bin/sh (qemu-user can't exec another emulated program);
// the long-running Windows child is this program in --child-sleep mode.
#ifdef _WIN32
static char self_path[1024];
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

static int spawn_sleeper(dino_process_t *p)
{
#ifdef _WIN32
    char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    char *const argv[] = SHELL("exec sleep 60");
#endif
    return dino_process_spawn(p, argv, NULL, NULL);
}

UTEST(event, process_exit_reported_once)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    char *const    argv[] = SHELL("exit 3");
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    int ud;
    ASSERT_EQ(0, dino_event_add_process(loop, p, &ud));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 10000));
    EXPECT_EQ((uint32_t)DINO_EVENT_EXIT, out[0].events);
    EXPECT_EQ((void *)&ud, out[0].userdata);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 200));

    ASSERT_EQ(0, dino_event_del_process(loop, p));
    int code = -1;
    EXPECT_EQ(0, dino_process_wait_timeout(p, 0, &code));
    EXPECT_EQ(3, code);
    dino_event_loop_destroy(loop);
}

UTEST(event, process_kill_reported)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_process_t p;
    ASSERT_EQ(0, spawn_sleeper(&p));
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));

    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));
    ASSERT_EQ(0, dino_process_kill(p));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 10000));
    EXPECT_EQ((uint32_t)DINO_EVENT_EXIT, out[0].events);

    ASSERT_EQ(0, dino_event_del_process(loop, p));
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(p, &code));
    EXPECT_EQ(DINO_EXIT_KILLED, code);
    dino_event_loop_destroy(loop);
}

UTEST(event, process_deleted_before_it_exits)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_process_t p;
    ASSERT_EQ(0, spawn_sleeper(&p));
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));
    ASSERT_EQ(0, dino_event_del_process(loop, p));
    ASSERT_EQ(0, dino_process_kill(p));

    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 300));
    EXPECT_EQ(0, dino_process_wait(p, NULL));
    dino_event_loop_destroy(loop);
}

UTEST(event, process_already_exited_when_added)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    char *const    argv[] = SHELL("exit 0");
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    // Exited (a zombie on Linux) but not yet waited for: still a valid handle.
#ifdef _WIN32
    WaitForSingleObject((HANDLE)p, 5000);
#else
    dino_sleep_ms(500);
#endif
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_EQ((uint32_t)DINO_EVENT_EXIT, out[0].events);
    ASSERT_EQ(0, dino_event_del_process(loop, p));
    EXPECT_EQ(0, dino_process_wait(p, NULL));
    dino_event_loop_destroy(loop);
}

UTEST(event, process_invalid_args)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_process_t p;
    ASSERT_EQ(0, spawn_sleeper(&p));
    EXPECT_EQ(-1, dino_event_add_process(loop, DINO_INVALID_PROCESS, NULL));
    EXPECT_EQ(-1, dino_event_add_process(DINO_INVALID_EVENT_LOOP, p, NULL));
    EXPECT_EQ(-1, dino_event_del_process(loop, p));
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));
    EXPECT_EQ(-1, dino_event_add_process(loop, p, NULL));
    ASSERT_EQ(0, dino_event_del_process(loop, p));
    EXPECT_EQ(-1, dino_event_del_process(loop, p));

    dino_process_kill(p);
    dino_process_wait(p, NULL);
    dino_event_loop_destroy(loop);
}

UTEST(event, many_processes_in_one_loop)
{
    enum
    {
        N = 20
    };
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    static dino_process_t procs[N];
    static int            seen[N];
    char *const           argv[] = SHELL("exit 0");
    for (int i = 0; i < N; i++)
    {
        ASSERT_EQ(0, dino_process_spawn(&procs[i], argv, NULL, NULL));
        ASSERT_EQ(0, dino_event_add_process(loop, procs[i], &seen[i]));
    }

    int          exits = 0;
    dino_event_t out[8];
    uint64_t     start = dino_time_monotonic_ns();
    while (exits < N && dino_time_monotonic_ns() - start < 20000000000ull)
    {
        int n = dino_event_wait(loop, out, 8, 1000);
        ASSERT_GE(n, 0);
        for (int i = 0; i < n; i++)
        {
            ASSERT_EQ((uint32_t)DINO_EVENT_EXIT, out[i].events);
            int idx = (int)((int *)out[i].userdata - seen);
            ASSERT_TRUE(idx >= 0 && idx < N);
            EXPECT_EQ(0, seen[idx]++);
            exits++;
        }
    }
    EXPECT_EQ(N, exits);
    for (int i = 0; i < N; i++)
    {
        EXPECT_EQ(0, dino_event_del_process(loop, procs[i]));
        EXPECT_EQ(0, dino_process_wait(procs[i], NULL));
    }
    dino_event_loop_destroy(loop);
}

// Destroyed with a process still registered and running: on Windows its wait
// registration must be cancelled, not left to fire into a freed loop.
UTEST(event, destroy_with_a_process_still_registered)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_process_t p;
    ASSERT_EQ(0, spawn_sleeper(&p));
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));
    dino_event_loop_destroy(loop);

    ASSERT_EQ(0, dino_process_kill(p));
    dino_sleep_ms(100);
    EXPECT_EQ(0, dino_process_wait(p, NULL));
}

UTEST_STATE();

#ifdef _WIN32
static int child_main(const char *mode)
{
    if (strcmp(mode, "--child-sleep") == 0)
    {
        dino_sleep_ms(60000);
        return 0;
    }
    return 2;
}
#endif

int main(int argc, const char *const argv[])
{
#ifdef _WIN32
    if (argc > 1 && strncmp(argv[1], "--child-", 8) == 0)
        return child_main(argv[1]);
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));
#endif
    return utest_main(argc, argv);
}
