// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <winsock2.h>

#include <windows.h>
#endif

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>
#include <dino/core/time.h>

#ifndef _WIN32
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#endif

#include "../src/core/thread_internal.h"
#include "net_helpers.h"

struct pair
{
    dino_socket_t server, client, peer;
};

// peer is the accepted end and is non-blocking; client writes into it.
static int open_pair(struct pair *p)
{
    p->server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 1);
    p->client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    p->peer   = DINO_INVALID_SOCKET;
    if (p->server == DINO_INVALID_SOCKET || p->client == DINO_INVALID_SOCKET)
        return -1;
    if (connect_to(p->client, "127.0.0.1", local_port(p->server)) != 0)
        return -1;
    p->peer = dino_socket_accept(p->server, NULL, NULL);
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

// A failed setup skips the teardown, so it releases what it made itself:
// otherwise the leak check would blame the failure on a leak.
UTEST_F_SETUP(event)
{
    utest_fixture->loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, utest_fixture->loop);
    int rc = open_pair(&utest_fixture->p);
    if (rc != 0)
    {
        close_pair(&utest_fixture->p);
        dino_event_loop_destroy(utest_fixture->loop);
    }
    ASSERT_EQ(0, rc);
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

    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
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
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
        EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    }

    char c;
    ASSERT_EQ(1, recv_some(utest_fixture->p.peer, &c, 1));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 100));
}

UTEST_F(event, oneshot_rearm)
{
    dino_event_t out[8];
    uint32_t     interest = DINO_EVENT_READ | DINO_EVENT_ONESHOT;
    ASSERT_EQ(0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, interest, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
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

    // READ too: a recv returns 0 now, and a handler that only looks at READ
    // must see the end of the stream on every platform.
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    char c;
    EXPECT_EQ(0, recv_some(utest_fixture->p.peer, &c, 1));
}

// With no READ or WRITE interest, a peer's FIN is not reported: it lasts, and
// would end every wait at once for as long as the socket is in the loop.
UTEST_F(event, half_closed_peer_is_quiet_without_interest)
{
    dino_event_t out[8];
    ASSERT_EQ(0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, 0, NULL));
    ASSERT_EQ(0, shutdown(utest_fixture->p.client, SHUT_WR));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 200));

    // Asked for again, it is there.
    ASSERT_EQ(0,
        dino_event_mod_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);
}

// Shut down both ways, which is EPOLLHUP on Linux: still only what was asked
// for, plus HANGUP.
UTEST_F(event, hangup_reports_only_the_interest)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_WRITE, NULL));
    dino_socket_close(utest_fixture->p.client);
    utest_fixture->p.client = DINO_INVALID_SOCKET;
    ASSERT_EQ(0, shutdown(utest_fixture->p.peer, SHUT_WR));

    int hangup = 0;
    for (int i = 0; i < 10 && !hangup; i++)
    {
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
        EXPECT_FALSE(out[0].events & DINO_EVENT_READ);
        hangup = (out[0].events & DINO_EVENT_HANGUP) != 0;
    }
    EXPECT_TRUE(hangup);
}

UTEST_F(event, del_stops_events)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));

    ASSERT_EQ(0, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 100));
    // And it can be added again.
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));
}

// Closing without del is a mistake, but it must not report the dead socket,
// whose userdata is usually freed by then: the loop forgets it, as epoll does.
UTEST_F(event, socket_closed_without_del_is_forgotten)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 0));

    dino_socket_close(utest_fixture->p.peer);
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 200));
    EXPECT_EQ(-1, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    utest_fixture->p.peer = DINO_INVALID_SOCKET;
}

// The same when it was closed between being reported and the next wait, which
// is when a level-triggered socket is polled again.
UTEST_F(event, socket_closed_after_an_event_is_forgotten)
{
    dino_event_t out[8];
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 8, 1000));

    dino_socket_close(utest_fixture->p.peer);
    EXPECT_EQ(0, dino_event_wait(utest_fixture->loop, out, 8, 200));
    EXPECT_EQ(-1, dino_event_del_socket(utest_fixture->loop, utest_fixture->p.peer));
    utest_fixture->p.peer = DINO_INVALID_SOCKET;
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
    ASSERT_EQ(0, open_pair(&q));
    int          a, b;
    dino_event_t out[1];
    ASSERT_EQ(
        0, dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, &a));
    ASSERT_EQ(0,
        dino_event_add_socket(
            utest_fixture->loop, q.peer, DINO_EVENT_READ | DINO_EVENT_ONESHOT, &b));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    ASSERT_EQ(1, dino_socket_send(q.client, "y", 1, 0));
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
        socks[i] = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
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
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wake_later, &loop));

    dino_event_t out[4];
    uint64_t     start = dino_time_monotonic_ns();
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_LT((dino_time_monotonic_ns() - start) / 1000000, 4000u);
    EXPECT_EQ((uint32_t)DINO_EVENT_WAKE, out[0].events);
    EXPECT_EQ(NULL, out[0].userdata);

    int result = -1;
    EXPECT_EQ(0, dino__thread_join(t, &result));
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
    dino_event_timer_t timer = dino_event_add_timer(loop, 50, 0, &x);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_GE(elapsed_ms(start), 50u);
    EXPECT_EQ((uint32_t)DINO_EVENT_TIMER, out[0].events);
    EXPECT_EQ((void *)&x, out[0].userdata);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));

    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, timer_repeats)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    // Timed from before the add: the fifth firing is 100 ms after the add,
    // which can be under 100 ms after a clock read taken later.
    uint64_t           start = dino_time_monotonic_ns();
    dino_event_timer_t timer = dino_event_add_timer(loop, 20, 20, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    dino_event_t out[4];
    int          fired = 0;
    while (fired < 5 && elapsed_ms(start) < 5000)
    {
        int n = dino_event_wait(loop, out, 4, 1000);
        ASSERT_GE(n, 1);
        for (int i = 0; i < n; i++)
            fired += out[i].events == DINO_EVENT_TIMER;
    }
    EXPECT_EQ(5, fired);
    EXPECT_GE(elapsed_ms(start), 100u);
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
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
        timers[i] = dino_event_add_timer(loop, (uint32_t)ids[i], 0, &ids[i]);
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
        EXPECT_EQ(0, dino_event_del_timer(loop, timers[i]));
    dino_event_loop_destroy(loop);
}

UTEST(event, timer_deleted_before_it_fires)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t keep  = dino_event_add_timer(loop, 10000, 0, NULL);
    dino_event_timer_t timer = dino_event_add_timer(loop, 50, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));

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
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wait_long, &w));
    dino_sleep_ms(50);

    uint64_t           start = dino_time_monotonic_ns();
    dino_event_timer_t timer = dino_event_add_timer(w.loop, 10, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    EXPECT_EQ(0, dino__thread_join(t, NULL));
    EXPECT_LT(elapsed_ms(start), 5000u);
    EXPECT_EQ(1, w.n);
    EXPECT_EQ((uint32_t)DINO_EVENT_TIMER, w.out[0].events);

    EXPECT_EQ(0, dino_event_del_timer(w.loop, timer));
    dino_event_loop_destroy(w.loop);
}

UTEST_F(event, timer_and_socket_in_one_wait)
{
    dino_event_timer_t timer = dino_event_add_timer(utest_fixture->loop, 0, 0, NULL);
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
    EXPECT_EQ(0, dino_event_del_timer(utest_fixture->loop, timer));
}

UTEST_F(event, timer_fires_while_a_socket_stays_ready)
{
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    uint64_t           start = dino_time_monotonic_ns();
    dino_event_timer_t timer = dino_event_add_timer(utest_fixture->loop, 10, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    int          fired = 0;
    dino_event_t out[1];
    while (!fired && elapsed_ms(start) < 2000)
    {
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 1, 100));
        fired = out[0].events == DINO_EVENT_TIMER;
    }
    EXPECT_TRUE(fired);
    EXPECT_EQ(0, dino_event_del_timer(utest_fixture->loop, timer));
}

// The reverse: a repeating timer that is due at every wait leaves room for I/O.
UTEST_F(event, due_timer_leaves_room_for_a_ready_socket)
{
    ASSERT_EQ(0,
        dino_event_add_socket(utest_fixture->loop, utest_fixture->p.peer, DINO_EVENT_READ, NULL));
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));
    dino_event_timer_t timer = dino_event_add_timer(utest_fixture->loop, 0, 1, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    int          timers = 0, reads = 0;
    dino_event_t out[1];
    for (int i = 0; i < 20; i++)
    {
        dino_sleep_ms(2);
        ASSERT_EQ(1, dino_event_wait(utest_fixture->loop, out, 1, 1000));
        timers += out[0].events == DINO_EVENT_TIMER;
        reads += (out[0].events & DINO_EVENT_READ) != 0;
    }
    EXPECT_GE(timers, 5);
    EXPECT_GE(reads, 5);
    EXPECT_EQ(0, dino_event_del_timer(utest_fixture->loop, timer));
}

UTEST(event, timer_invalid_args)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    EXPECT_EQ(DINO_INVALID_EVENT_TIMER, dino_event_add_timer(DINO_INVALID_EVENT_LOOP, 1, 0, NULL));
    EXPECT_EQ(-1, dino_event_del_timer(loop, DINO_INVALID_EVENT_TIMER));
    EXPECT_EQ(-1, dino_event_del_timer(DINO_INVALID_EVENT_LOOP, DINO_INVALID_EVENT_TIMER));
    dino_event_loop_destroy(loop);
}

// A timer belongs to the loop it was added to.
UTEST(event, timer_del_through_another_loop_fails)
{
    dino_event_loop_t a = dino_event_loop_create();
    dino_event_loop_t b = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, a);
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, b);
    dino_event_timer_t t = dino_event_add_timer(a, 10000, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, t);

    EXPECT_EQ(-1, dino_event_del_timer(b, t));
    EXPECT_EQ(0, dino_event_del_timer(a, t));
    dino_event_loop_destroy(a);
    dino_event_loop_destroy(b);
}

/* ---- timer reset ---- */

UTEST(event, mod_timer_pushes_the_deadline_back)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    int                x     = 0;
    dino_event_timer_t timer = dino_event_add_timer(loop, 50, 0, &x);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    uint64_t start = dino_time_monotonic_ns();
    ASSERT_EQ(0, dino_event_mod_timer(loop, timer, 300, 0));
    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 150));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_GE(elapsed_ms(start), 300u);
    EXPECT_EQ((void *)&x, out[0].userdata);
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, mod_timer_arms_a_fired_one_shot_again)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_add_timer(loop, 0, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));

    for (int round = 0; round < 3; round++)
    {
        ASSERT_EQ(0, dino_event_mod_timer(loop, timer, 10, 0));
        EXPECT_EQ(1, dino_event_wait(loop, out, 4, 1000));
        EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));
    }
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

// Due, but no wait has reported it yet: the reset moves it, so nothing fires.
UTEST(event, mod_timer_drops_a_firing_not_yet_reported)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_add_timer(loop, 0, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    dino_sleep_ms(20);
    ASSERT_EQ(0, dino_event_mod_timer(loop, timer, 5000, 0));
    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, mod_timer_changes_the_interval)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_add_timer(loop, 10000, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    ASSERT_EQ(0, dino_event_mod_timer(loop, timer, 10, 10));

    dino_event_t out[4];
    int          fired = 0;
    uint64_t     start = dino_time_monotonic_ns();
    while (fired < 3 && elapsed_ms(start) < 5000)
    {
        int n = dino_event_wait(loop, out, 4, 1000);
        ASSERT_GE(n, 0);
        fired += n;
    }
    EXPECT_EQ(3, fired);
    // And back to a one-shot.
    ASSERT_EQ(0, dino_event_mod_timer(loop, timer, 10, 0));
    EXPECT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

// Earlier while another thread waits: that wait ends at the new deadline, not
// the one it started with.
UTEST(event, mod_timer_earlier_shortens_a_running_wait)
{
    struct blocked_wait w = { 0 };
    w.loop                = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, w.loop);
    dino_event_timer_t timer = dino_event_add_timer(w.loop, 8000, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wait_long, &w));
    dino_sleep_ms(50);

    uint64_t start = dino_time_monotonic_ns();
    ASSERT_EQ(0, dino_event_mod_timer(w.loop, timer, 10, 0));
    EXPECT_EQ(0, dino__thread_join(t, NULL));
    EXPECT_LT(elapsed_ms(start), 5000u);
    EXPECT_EQ(1, w.n);
    EXPECT_EQ((uint32_t)DINO_EVENT_TIMER, w.out[0].events);
    EXPECT_EQ(0, dino_event_del_timer(w.loop, timer));
    dino_event_loop_destroy(w.loop);
}

UTEST(event, mod_timer_invalid_args)
{
    dino_event_loop_t a = dino_event_loop_create();
    dino_event_loop_t b = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, a);
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, b);
    dino_event_timer_t t = dino_event_add_timer(a, 10000, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, t);
    EXPECT_EQ(-1, dino_event_mod_timer(DINO_INVALID_EVENT_LOOP, t, 0, 0));
    EXPECT_EQ(-1, dino_event_mod_timer(a, DINO_INVALID_EVENT_TIMER, 0, 0));
    EXPECT_EQ(-1, dino_event_mod_timer(b, t, 0, 0));
    EXPECT_EQ(0, dino_event_del_timer(a, t));
    dino_event_loop_destroy(a);
    dino_event_loop_destroy(b);
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

static int start_waiter(struct waiter *w, dino__thread_t *t)
{
    atomic_store(&w->started, 0);
    if (dino__thread_create(t, wait_once, w) != 0)
        return -1;
    while (!atomic_load(&w->started))
        dino__thread_yield();
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
        dino_socket_t s = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(DINO_INVALID_SOCKET, s);
        ASSERT_EQ(0, dino_event_add_socket(w.loop, s, DINO_EVENT_READ, &w));

        dino__thread_t t;
        ASSERT_EQ(0, start_waiter(&w, &t));
        ASSERT_EQ(0, dino_event_del_socket(w.loop, s));
        ASSERT_EQ(0, dino_socket_close(s));
        ASSERT_EQ(0, dino_event_wake(w.loop));
        ASSERT_EQ(0, dino__thread_join(t, NULL));

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
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    ASSERT_EQ(0, dino_event_add_socket(w.loop, s, DINO_EVENT_READ, NULL));

    dino__thread_t t;
    ASSERT_EQ(0, start_waiter(&w, &t));
    uint64_t start = dino_time_monotonic_ns();
    ASSERT_EQ(0, dino_event_mod_socket(w.loop, s, DINO_EVENT_WRITE, &w));
    ASSERT_EQ(0, dino__thread_join(t, NULL));
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
    dino__thread_t       t[WAITERS];
    for (int i = 0; i < WAITERS; i++)
    {
        w[i].loop       = utest_fixture->loop;
        w[i].timeout_ms = 1500;
        ASSERT_EQ(0, start_waiter(&w[i], &t[i]));
    }
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "x", 1, 0));

    int reads = 0;
    for (int i = 0; i < WAITERS; i++)
    {
        ASSERT_EQ(0, dino__thread_join(t[i], NULL));
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

// HANGUP means a read returns 0, on every platform: not while data written
// before the last writer closed is still waiting.
UTEST(event, pipe_hangup_waits_until_the_data_is_read)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    ASSERT_EQ(4, dino_pipe_write(w, "tail", 4));
    ASSERT_EQ(0, dino_pipe_close(w));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    EXPECT_FALSE(out[0].events & DINO_EVENT_HANGUP);
    char buf[8];
    EXPECT_EQ(4, dino_pipe_read(r, buf, sizeof(buf)));

    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 1000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    EXPECT_TRUE(out[0].events & DINO_EVENT_HANGUP);
    EXPECT_EQ(0, dino_pipe_read(r, buf, sizeof(buf)));

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

// Windows reuses handle values, so a new loop's port can get the destroyed
// loop's value. The pipe is still attached to the dead port and must not pass
// as attached to the new one.
UTEST(event, pipe_refused_by_a_later_loop_on_windows)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    for (int i = 0; i < 8; i++)
    {
        dino_event_loop_t a = dino_event_loop_create();
        if (i == 0)
            ASSERT_EQ(0, dino_event_add_pipe(a, r, DINO_EVENT_READ, NULL));
        else
            EXPECT_EQ(-1, dino_event_add_pipe(a, r, DINO_EVENT_READ, NULL));
        dino_event_del_pipe(a, r);
        dino_event_loop_destroy(a);
    }
    dino_pipe_close(r);
    dino_pipe_close(w);
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
    ASSERT_EQ(1, dino_socket_send(utest_fixture->p.client, "y", 1, 0));
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
    const char *const         argv[] = SHELL_ECHO;
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
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(proc, &status));
    EXPECT_EQ(0, status.code);
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
    const char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    const char *const argv[] = SHELL("exec sleep 60");
#endif
    return dino_process_spawn(p, argv, NULL, NULL);
}

UTEST(event, process_exit_reported_once)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    const char *const argv[] = SHELL("exit 3");
    dino_process_t    p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    int ud;
    ASSERT_EQ(0, dino_event_add_process(loop, p, &ud));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 10000));
    EXPECT_EQ((uint32_t)DINO_EVENT_EXIT, out[0].events);
    EXPECT_EQ((void *)&ud, out[0].userdata);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 200));

    ASSERT_EQ(0, dino_event_del_process(loop, p));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait_timeout(p, 0, &status));
    EXPECT_EQ(3, status.code);
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
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(DINO_EXIT_KILLED, status.code);
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
    const char *const argv[] = SHELL("exit 0");
    dino_process_t    p;
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
    const char *const     argv[] = SHELL("exit 0");
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

/* ---- connections ---- */

UTEST(event, listener_is_readable_with_a_connection_to_accept)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_socket_t server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 4);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    ASSERT_EQ(0, dino_event_add_socket(loop, server, DINO_EVENT_READ, &server));

    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, connect_to(client, "127.0.0.1", local_port(server)));
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_READ);
    EXPECT_EQ((void *)&server, out[0].userdata);

    dino_socket_t peer = dino_socket_accept(server, NULL, NULL);
    EXPECT_NE(DINO_INVALID_SOCKET, peer);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));

    dino_event_del_socket(loop, server);
    dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
    dino_event_loop_destroy(loop);
}

UTEST(event, nonblocking_connect_reports_write_once_connected)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_socket_t server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 4);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_EQ(0, dino_socket_set_nonblocking(client, 1));

    int rc = connect_to(client, "127.0.0.1", local_port(server));
    ASSERT_TRUE(rc == 0 || dino_error() == DINO_EINPROGRESS);
    ASSERT_EQ(0, dino_event_add_socket(loop, client, DINO_EVENT_WRITE, NULL));
    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_TRUE(out[0].events & DINO_EVENT_WRITE);
    EXPECT_FALSE(out[0].events & (DINO_EVENT_ERROR | DINO_EVENT_HANGUP));
    EXPECT_EQ(0, dino_socket_error(client));

    dino_event_del_socket(loop, client);
    dino_socket_close(client);
    dino_socket_close(server);
    dino_event_loop_destroy(loop);
}

static uint32_t refused_connect_events(uint32_t interest)
{
    dino_socket_t bound;
    int           port = refusing_port(&bound);
    if (port < 0)
        return 0;
    dino_event_loop_t loop   = dino_event_loop_create();
    dino_socket_t     client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    uint32_t          events = 0;
    dino_socket_set_nonblocking(client, 1);
    if (connect_to(client, "127.0.0.1", port) != 0 && dino_error() == DINO_EINPROGRESS
        && dino_event_add_socket(loop, client, interest, NULL) == 0)
    {
        dino_event_t out[4];
        // Windows retries a refused connect for about a second before it gives up.
        if (dino_event_wait(loop, out, 4, 10000) == 1
            && dino_socket_error(client) == DINO_ECONNREFUSED)
            events = out[0].events;
        dino_event_del_socket(loop, client);
    }
    dino_socket_close(client);
    dino_socket_close(bound);
    dino_event_loop_destroy(loop);
    return events;
}

// As docs/sdk/event.md describes it: ERROR, with WRITE if watched for it; on
// Linux HANGUP too, and READ if watched for it.
UTEST(event, refused_nonblocking_connect_reports_error)
{
    uint32_t write_only = refused_connect_events(DINO_EVENT_WRITE);
    EXPECT_TRUE(write_only & DINO_EVENT_ERROR);
    EXPECT_TRUE(write_only & DINO_EVENT_WRITE);
    EXPECT_FALSE(write_only & DINO_EVENT_READ);

    uint32_t read_only = refused_connect_events(DINO_EVENT_READ);
    EXPECT_TRUE(read_only & DINO_EVENT_ERROR);
    EXPECT_FALSE(read_only & DINO_EVENT_WRITE);
#ifndef _WIN32
    EXPECT_TRUE(read_only & DINO_EVENT_HANGUP);
    EXPECT_TRUE(read_only & DINO_EVENT_READ);
#endif

    EXPECT_TRUE(refused_connect_events(0) & DINO_EVENT_ERROR);
}

/* ---- more timers and waits ---- */

// More timers than the heap starts with, added out of order, some deleted
// from the middle before they fire.
UTEST(event, many_timers_fire_in_order_after_deletes)
{
    enum
    {
        N = 24
    };
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    static int         ids[N];
    dino_event_timer_t timers[N];
    for (int i = 0; i < N; i++)
    {
        int order = (i * 7) % N; // every order once, shuffled
        ids[i]    = order;
        timers[i] = dino_event_add_timer(loop, (uint32_t)(10 + 5 * order), 0, &ids[i]);
        ASSERT_NE(DINO_INVALID_EVENT_TIMER, timers[i]);
    }
    for (int i = 0; i < N; i += 3)
        ASSERT_EQ(0, dino_event_del_timer(loop, timers[i]));

    int          last = -1, fired = 0;
    dino_event_t out[4];
    uint64_t     start = dino_time_monotonic_ns();
    while (fired < N - (N + 2) / 3 && elapsed_ms(start) < 10000)
    {
        int n = dino_event_wait(loop, out, 4, 1000);
        ASSERT_GE(n, 0);
        for (int j = 0; j < n; j++)
        {
            int order = *(int *)out[j].userdata;
            EXPECT_GT(order, last);
            EXPECT_NE(0, (int)(((int *)out[j].userdata - ids) % 3));
            last = order;
            fired++;
        }
    }
    EXPECT_EQ(N - (N + 2) / 3, fired);
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));
    for (int i = 0; i < N; i++)
        if (i % 3)
            EXPECT_EQ(0, dino_event_del_timer(loop, timers[i]));
    dino_event_loop_destroy(loop);
}

UTEST(event, timers_deleted_right_after_they_fire_stay_quiet)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t once   = dino_event_add_timer(loop, 0, 0, NULL);
    dino_event_timer_t repeat = dino_event_add_timer(loop, 0, 20, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, once);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, repeat);

    dino_event_t out[4];
    ASSERT_EQ(2, dino_event_wait(loop, out, 4, 1000));
    EXPECT_EQ(0, dino_event_del_timer(loop, once));
    EXPECT_EQ(0, dino_event_del_timer(loop, repeat));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 100));
    dino_event_loop_destroy(loop);
}

UTEST(event, repeating_timer_that_falls_behind_fires_once)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_add_timer(loop, 100, 100, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);

    dino_sleep_ms(550); // five periods missed
    dino_event_t out[8];
    EXPECT_EQ(1, dino_event_wait(loop, out, 8, 0));
    EXPECT_EQ(0, dino_event_wait(loop, out, 8, 0));
    // And it keeps going.
    EXPECT_EQ(1, dino_event_wait(loop, out, 8, 5000));
    EXPECT_EQ(0, dino_event_del_timer(loop, timer));
    dino_event_loop_destroy(loop);
}

UTEST(event, wait_runs_out_no_earlier_than_the_timeout)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_t out[4];
    for (int i = 0; i < 20; i++)
    {
        uint64_t start = dino_time_monotonic_ns();
        ASSERT_EQ(0, dino_event_wait(loop, out, 4, 5));
        ASSERT_GE(dino_time_monotonic_ns() - start, 5u * 1000000u);
    }
    dino_event_loop_destroy(loop);
}

UTEST(event, waiting_forever_returns_for_a_wake)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, wake_later, &loop));
    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, -1));
    EXPECT_EQ((uint32_t)DINO_EVENT_WAKE, out[0].events);
    int result = -1;
    EXPECT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(0, result);
    dino_event_loop_destroy(loop);
}

static int wake_many_times(void *arg)
{
    for (int i = 0; i < 200; i++)
        if (dino_event_wake(*(dino_event_loop_t *)arg) != 0)
            return -1;
    return 0;
}

UTEST(event, wakes_from_many_threads_merge)
{
    enum
    {
        THREADS = 8
    };
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino__thread_t t[THREADS];
    for (int i = 0; i < THREADS; i++)
        ASSERT_EQ(0, dino__thread_create(&t[i], wake_many_times, &loop));

    dino_event_t out[4];
    ASSERT_EQ(1, dino_event_wait(loop, out, 4, 5000));
    EXPECT_EQ((uint32_t)DINO_EVENT_WAKE, out[0].events);
    for (int i = 0; i < THREADS; i++)
    {
        int result = -1;
        EXPECT_EQ(0, dino__thread_join(t[i], &result));
        EXPECT_EQ(0, result);
    }
    // Whatever arrived after that report is one more wake at most.
    int n = dino_event_wait(loop, out, 4, 0);
    EXPECT_TRUE(n == 0 || (n == 1 && out[0].events == (uint32_t)DINO_EVENT_WAKE));
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 50));
    dino_event_loop_destroy(loop);
}

// More ready items than the loop collects in one batch, and an out array
// bigger than that batch.
UTEST(event, out_array_larger_than_the_internal_batch)
{
    enum
    {
        N = 150
    };
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    static dino_socket_t socks[N];
    static int           seen[N];
    for (int i = 0; i < N; i++)
    {
        socks[i] = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
        ASSERT_NE(DINO_INVALID_SOCKET, socks[i]);
        ASSERT_EQ(0,
            dino_event_add_socket(
                loop, socks[i], DINO_EVENT_WRITE | DINO_EVENT_ONESHOT, &socks[i]));
    }
    static dino_event_t out[N + 10];
    int                 total = 0;
    for (int round = 0; round < 20 && total < N; round++)
    {
        int n = dino_event_wait(loop, out, N + 10, 1000);
        ASSERT_GE(n, 1);
        ASSERT_LE(n, N);
        for (int i = 0; i < n; i++)
        {
            int idx = (int)((dino_socket_t *)out[i].userdata - socks);
            ASSERT_TRUE(idx >= 0 && idx < N);
            EXPECT_EQ(0, seen[idx]++);
            total++;
        }
    }
    EXPECT_EQ(N, total);
    for (int i = 0; i < N; i++)
    {
        dino_event_del_socket(loop, socks[i]);
        dino_socket_close(socks[i]);
    }
    dino_event_loop_destroy(loop);
}

// The pipe outlives the loop and still works.
UTEST(event, destroy_with_a_pipe_still_registered)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_EQ(0, dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL));
    dino_event_t out[4];
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 0));
    dino_event_loop_destroy(loop);

    EXPECT_EQ(2, dino_pipe_write(w, "ok", 2));
    char buf[4] = { 0 };
    EXPECT_EQ(2, dino_pipe_read(r, buf, sizeof(buf)));
    EXPECT_STREQ("ok", buf);
    dino_pipe_close(r);
    dino_pipe_close(w);
}

#ifndef _WIN32
static void ignore_signal(int sig)
{
    (void)sig;
}

static int signal_later(void *arg)
{
    dino_sleep_ms(30);
    return pthread_kill(*(pthread_t *)arg, SIGUSR1);
}

// Interrupted by a signal, a wait goes on for the rest of its time.
UTEST(event, a_signal_doesnt_cut_a_wait_short)
{
    struct sigaction sa = { 0 }, old;
    sa.sa_handler       = ignore_signal; // no SA_RESTART: epoll_wait fails with EINTR
    sigemptyset(&sa.sa_mask);
    ASSERT_EQ(0, sigaction(SIGUSR1, &sa, &old));

    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    pthread_t      self = pthread_self();
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, signal_later, &self));
    dino_event_t out[4];
    uint64_t     start = dino_time_monotonic_ns();
    EXPECT_EQ(0, dino_event_wait(loop, out, 4, 200));
    EXPECT_GE(elapsed_ms(start), 200u);

    int result = -1;
    EXPECT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(0, result);
    dino_event_loop_destroy(loop);
    sigaction(SIGUSR1, &old, NULL);
}

// Every descriptor a loop opens is close-on-exec.
UTEST(event, loop_descriptors_are_not_inherited)
{
    enum
    {
        MAX_FD = 256
    };
    static char before[MAX_FD];
    for (int fd = 0; fd < MAX_FD; fd++)
        before[fd] = fcntl(fd, F_GETFD) >= 0;

    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_event_timer_t timer = dino_event_add_timer(loop, 1000, 0, NULL);
    ASSERT_NE(DINO_INVALID_EVENT_TIMER, timer);
    dino_process_t    p;
    const char *const argv[] = SHELL("exit 0");
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    ASSERT_EQ(0, dino_event_add_process(loop, p, NULL));

    int added = 0;
    for (int fd = 0; fd < MAX_FD; fd++)
    {
        int flags = fcntl(fd, F_GETFD);
        if (flags < 0 || before[fd])
            continue;
        added++;
        EXPECT_TRUE(flags & FD_CLOEXEC);
    }
    EXPECT_GE(added, 1);

    dino_event_del_process(loop, p);
    dino_process_wait(p, NULL);
    dino_event_del_timer(loop, timer);
    dino_event_loop_destroy(loop);
}
#endif

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
