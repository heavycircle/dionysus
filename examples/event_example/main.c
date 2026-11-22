#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <sys/resource.h>
#endif

#include <dino/core/error.h>
#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/thread.h>
#include <dino/core/time.h>

#define PORT        8091
#define CONNECTIONS 1000
#define MSG_LEN     16
#define DEADLINE_MS 30000

// One echo server thread and one client (main), each with its own event loop.
// The client opens every connection before reading any reply, so the server
// holds all 1000 at once.

struct server
{
    dino_event_loop_t loop;
    dino_socket_t     listener;
    int               peak, open;
};

struct conn
{
    dino_socket_t sock;
    char          sent[MSG_LEN];
    char          got[MSG_LEN];
    int           len;
};

static struct conn conns[CONNECTIONS];

static void serve_accept(struct server *srv)
{
    // Non-blocking: accept until the backlog is empty.
    dino_socket_t s;
    while ((s = dino_socket_accept(srv->listener)) != DINO_INVALID_SOCKET)
    {
        dino_socket_set_nonblocking(s, 1);
        if (dino_event_add_socket(srv->loop, s, DINO_EVENT_READ, (void *)s) != 0)
        {
            dino_socket_close(s);
            continue;
        }
        if (++srv->open > srv->peak)
            srv->peak = srv->open;
    }
    // DINO_EAGAIN is the empty backlog. Anything else is a real failure, with
    // the connection, if there was one, still waiting.
    if (dino_error() != DINO_EAGAIN)
        fprintf(stderr, "accept: %s\n", dino_error_message(dino_error()));
}

static void serve_conn(struct server *srv, dino_socket_t s, uint32_t events)
{
    char buf[256];
    int  n = dino_socket_recv(s, buf, sizeof(buf));
    if (n > 0)
    {
        // Loopback socket buffers are far larger than one message, so the
        // echo goes out whole.
        dino_socket_send(s, buf, n);
        return;
    }
    // n < 0 without HANGUP/ERROR is just "nothing to read after all".
    if (n == 0 || (events & (DINO_EVENT_HANGUP | DINO_EVENT_ERROR)))
    {
        dino_event_del_socket(srv->loop, s);
        dino_socket_close(s);
        srv->open--;
    }
}

static int run_server(void *arg)
{
    struct server *srv = arg;
    dino_event_t   events[64];
    for (;;)
    {
        int n = dino_event_wait(srv->loop, events, 64, -1);
        if (n < 0)
            return -1;
        for (int i = 0; i < n; i++)
        {
            if (events[i].events & DINO_EVENT_WAKE)
                return 0;
            if (events[i].userdata == NULL)
                serve_accept(srv);
            else
                serve_conn(srv, (dino_socket_t)events[i].userdata, events[i].events);
        }
    }
}

static dino_socket_t listen_on(int port)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (s == DINO_INVALID_SOCKET)
        return s;
    dino_socket_set_reuseaddr(s, 1);
    if (dino_socket_bind(s, "127.0.0.1", port) != 0 || dino_socket_listen(s, 128) != 0
        || dino_socket_set_nonblocking(s, 1) != 0)
    {
        dino_socket_close(s);
        return DINO_INVALID_SOCKET;
    }
    return s;
}

// Returns how many replies matched what was sent.
static int run_clients(dino_event_loop_t loop)
{
    for (int i = 0; i < CONNECTIONS; i++)
        conns[i].sock = DINO_INVALID_SOCKET;

    int opened = 0;
    for (int i = 0; i < CONNECTIONS; i++)
    {
        struct conn *c = &conns[i];
        c->sock        = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
        if (c->sock == DINO_INVALID_SOCKET || dino_socket_connect(c->sock, "127.0.0.1", PORT) != 0)
        {
            printf("[Client] connection %d failed (FAILED)\n", i);
            break;
        }
        snprintf(c->sent, sizeof(c->sent), "conn %04d ping!", i);
        dino_socket_set_nonblocking(c->sock, 1);
        if (dino_event_add_socket(loop, c->sock, DINO_EVENT_READ, c) != 0
            || dino_socket_send(c->sock, c->sent, MSG_LEN) != MSG_LEN)
        {
            printf("[Client] connection %d couldn't send (FAILED)\n", i);
            break;
        }
        opened++;
    }
    printf("[Client] %d connections open\n", opened);

    dino_event_timer_t deadline = dino_event_add_timer(loop, DEADLINE_MS, 0, NULL);
    int                done = 0, matched = 0;
    dino_event_t       events[64];
    while (done < opened)
    {
        int n = dino_event_wait(loop, events, 64, -1);
        if (n < 0)
            break;
        for (int i = 0; i < n; i++)
        {
            if (events[i].events & DINO_EVENT_TIMER)
            {
                printf(
                    "[Client] %d of %d replies after %d ms (FAILED)\n", done, opened, DEADLINE_MS);
                done = opened;
                break;
            }
            struct conn *c = events[i].userdata;
            int          r = dino_socket_recv(c->sock, c->got + c->len, MSG_LEN - c->len);
            if (r > 0)
                c->len += r;
            if (c->len < MSG_LEN && r != 0)
                continue;
            matched += c->len == MSG_LEN && memcmp(c->got, c->sent, MSG_LEN) == 0;
            done++;
            dino_event_del_socket(loop, c->sock);
        }
    }
    dino_event_del_timer(loop, deadline);
    for (int i = 0; i < CONNECTIONS; i++)
    {
        if (conns[i].sock != DINO_INVALID_SOCKET)
            dino_socket_close(conns[i].sock);
    }
    return matched;
}

int main(void)
{
#ifndef _WIN32
    // Both ends of every connection live in this process: ~2000 descriptors,
    // past the usual soft limit of 1024.
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur < rl.rlim_max)
    {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif

    struct server srv = { 0 };
    srv.loop          = dino_event_loop_create();
    srv.listener      = listen_on(PORT);
    if (srv.loop == DINO_INVALID_EVENT_LOOP || srv.listener == DINO_INVALID_SOCKET
        || dino_event_add_socket(srv.loop, srv.listener, DINO_EVENT_READ, NULL) != 0)
    {
        printf("[Server] setup failed (FAILED)\n");
        return 1;
    }
    printf("[Server] Listening on port %d\n", PORT);

    dino_thread_t server_thread;
    if (dino_thread_create(&server_thread, run_server, &srv) != 0)
        return 1;

    dino_event_loop_t loop    = dino_event_loop_create();
    uint64_t          start   = dino_time_monotonic_ns();
    int               matched = loop == DINO_INVALID_EVENT_LOOP ? 0 : run_clients(loop);
    uint64_t          ms      = (dino_time_monotonic_ns() - start) / 1000000;

    dino_event_wake(srv.loop);
    dino_thread_join(server_thread, NULL);
    printf("[Server] peak of %d connections at once\n", srv.peak);
    printf("[Client] echoed %d/%d in %llu ms%s\n", matched, CONNECTIONS, (unsigned long long)ms,
        matched == CONNECTIONS ? "" : " (FAILED)");

    dino_event_loop_destroy(loop);
    dino_event_del_socket(srv.loop, srv.listener);
    dino_socket_close(srv.listener);
    dino_event_loop_destroy(srv.loop);
    return matched == CONNECTIONS ? 0 : 1;
}
