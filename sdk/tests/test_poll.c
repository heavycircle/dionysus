#include <poll.h>
#include <utest.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <dino/core/error.h>
#include <dino/core/network.h>
#include <dino/core/time.h>

#include "net_helpers.h"

// poll() and select() are the POSIX calls on every target; on Windows poll()
// is the SDK's, over WSAPoll (sdk/src/libc/poll_win32.c).

#define TIMEOUT_MS 5000
#define IDLE_MS    50

// What poll() takes as "no socket here": -1, or INVALID_SOCKET on Windows.
#define NO_FD ((dino_socket_t)~0)

struct pair
{
    dino_socket_t server, client, peer;
};

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

static unsigned elapsed_ms(uint64_t start)
{
    return (unsigned)((dino_time_monotonic_ns() - start) / 1000000);
}

UTEST(poll, idle_socket_times_out)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    struct pollfd pfd = { p.peer, POLLIN, POLLERR };
    EXPECT_EQ(0, poll(&pfd, 1, IDLE_MS));
    // revents is reset on timeout, not left from before.
    EXPECT_EQ(0, pfd.revents);
    close_pair(&p);
}

UTEST(poll, pending_connection_is_readable)
{
    dino_socket_t server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 1);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    ASSERT_EQ(0, connect_to(client, "127.0.0.1", local_port(server)));

    struct pollfd pfd = { server, POLLIN, 0 };
    EXPECT_EQ(1, poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & POLLIN);

    dino_socket_t peer = dino_socket_accept(server, NULL, NULL);
    EXPECT_NE(DINO_INVALID_SOCKET, peer);
    dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
}

UTEST(poll, data_is_readable_and_connected_is_writable)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4, 0));
    struct pollfd pfds[2] = { { p.peer, POLLIN, 0 }, { p.client, POLLOUT, 0 } };
    EXPECT_EQ(2, poll(pfds, 2, TIMEOUT_MS));
    EXPECT_TRUE(pfds[0].revents & POLLIN);
    EXPECT_TRUE(pfds[1].revents & POLLOUT);
    close_pair(&p);
}

UTEST(poll, peer_hangup_is_readable)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    dino_socket_close(p.client);
    p.client = DINO_INVALID_SOCKET;

    // A loop that only looks at POLLIN still calls recv and sees the end of
    // the stream: WSAPoll reports POLLHUP alone.
    struct pollfd pfd = { p.peer, POLLIN, 0 };
    EXPECT_EQ(1, poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & POLLIN);
    char buf[1];
    EXPECT_EQ(0, recv_some(p.peer, buf, sizeof(buf)));
    close_pair(&p);
}

// Nothing to watch is a sleep, as on Linux; WSAPoll fails at once instead, so
// a server whose client list emptied would spin.
UTEST(poll, nothing_to_watch_waits_out_the_timeout)
{
    uint64_t start = dino_time_monotonic_ns();
    EXPECT_EQ(0, poll(NULL, 0, 150));
    EXPECT_GE(elapsed_ms(start), 140u);
    EXPECT_EQ(0, poll(NULL, 0, 0));
}

// A negative descriptor is an unused slot: Linux's poll skips it, but WSAPoll
// reports it as an error and counts it.
UTEST(poll, unused_slots_are_skipped)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4, 0));
    struct pollfd pfds[3] = {
        { NO_FD, POLLIN, 0 },
        { p.peer, POLLIN, 0 },
        { NO_FD, POLLIN | POLLOUT, 0 },
    };
    EXPECT_EQ(1, poll(pfds, 3, TIMEOUT_MS));
    EXPECT_EQ(0, pfds[0].revents);
    EXPECT_TRUE(pfds[1].revents & POLLIN);
    EXPECT_EQ(0, pfds[2].revents);

    // Only unused slots: nothing to watch.
    uint64_t start = dino_time_monotonic_ns();
    EXPECT_EQ(0, poll(pfds, 1, 150));
    EXPECT_GE(elapsed_ms(start), 140u);
    close_pair(&p);
}

// POLLERR and POLLHUP are output-only; WSAPoll rejects them in events.
UTEST(poll, output_only_flags_in_events_are_ignored)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    struct pollfd pfd = { p.client, POLLOUT | POLLERR | POLLHUP, 0 };
    EXPECT_EQ(1, poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_TRUE(pfd.revents & POLLOUT);
    close_pair(&p);
}

UTEST(select, times_out_then_sees_data)
{
    struct pair p;
    ASSERT_EQ(0, open_pair(&p));
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(p.peer, &rfds);
    struct timeval tv = { 0, IDLE_MS * 1000 };
    EXPECT_EQ(0, select((int)p.peer + 1, &rfds, NULL, NULL, &tv));

    ASSERT_EQ(4, dino_socket_send(p.client, "ping", 4, 0));
    FD_ZERO(&rfds);
    FD_SET(p.peer, &rfds);
    tv.tv_sec  = TIMEOUT_MS / 1000;
    tv.tv_usec = 0;
    EXPECT_EQ(1, select((int)p.peer + 1, &rfds, NULL, NULL, &tv));
    EXPECT_TRUE(FD_ISSET(p.peer, &rfds));
    close_pair(&p);
}

// A connect that fails ends readable or writable with SO_ERROR set, which
// dino_socket_error reads.
UTEST(poll, failed_connect_reports_its_error)
{
    dino_socket_t holder;
    int           port = refusing_port(&holder);
    ASSERT_GT(port, 0);
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    ASSERT_EQ(0, dino_socket_set_nonblocking(s, 1));
    ASSERT_EQ(-1, connect_to(s, "127.0.0.1", port));
    EXPECT_EQ(DINO_EINPROGRESS, dino_error());

    struct pollfd pfd = { s, POLLOUT, 0 };
    EXPECT_EQ(1, poll(&pfd, 1, TIMEOUT_MS));
    EXPECT_EQ(DINO_ECONNREFUSED, dino_socket_error(s));
    dino_socket_close(s);
    dino_socket_close(holder);
}

UTEST_MAIN()
