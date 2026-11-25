#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#ifndef _WIN32
#include <sys/resource.h>
#endif

#include <dino/core/error.h>
#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/time.h>

#define CONNECTIONS 1000
#define MSG_LEN     16
#define DEADLINE_MS 30000

// One echo server thread and one client (main), each with its own event loop.
// The client opens every connection before reading any reply, so the server
// holds all 1000 at once.

// Each accepted connection, as its userdata: a pointer, never the socket
// value itself, which could be 0 and look like "no userdata".
struct peer
{
    dino_socket_t sock;
    struct peer  *next;
};

struct server
{
    dino_event_loop_t  loop;
    dino_socket_t      listener;
    struct peer       *peers; // every open connection, to close at shutdown
    dino_event_timer_t retry; // set while accepting is paused
    int                peak, open;
};

struct conn
{
    dino_socket_t sock;
    char          sent[MSG_LEN];
    char          got[MSG_LEN];
    int           len;
};

static struct conn conns[CONNECTIONS];

static void close_peer(struct server *srv, struct peer *p)
{
    dino_event_del_socket(srv->loop, p->sock);
    dino_socket_close(p->sock);
    for (struct peer **link = &srv->peers; *link; link = &(*link)->next)
    {
        if (*link == p)
        {
            *link = p->next;
            break;
        }
    }
    free(p);
    srv->open--;
}

static void serve_accept(struct server *srv)
{
    // Non-blocking: accept until the backlog is empty.
    dino_socket_t s;
    while ((s = dino_socket_accept(srv->listener, NULL, NULL)) != DINO_INVALID_SOCKET)
    {
        struct peer *p = malloc(sizeof(*p));
        dino_socket_set_nonblocking(s, 1);
        if (!p || dino_event_add_socket(srv->loop, s, DINO_EVENT_READ, p) != 0)
        {
            free(p);
            dino_socket_close(s);
            continue;
        }
        p->sock    = s;
        p->next    = srv->peers;
        srv->peers = p;
        if (++srv->open > srv->peak)
            srv->peak = srv->open;
    }
    // DINO_EAGAIN is the empty backlog. Anything else (out of descriptors, say)
    // leaves the connection waiting and the listener readable, so a
    // level-triggered loop would report it again at once and spin: stop
    // watching it for a moment instead.
    if (dino_error() != DINO_EAGAIN)
    {
        fprintf(stderr, "accept: %s; pausing for 100 ms\n", dino_error_message(dino_error()));
        srv->retry = dino_event_add_timer(srv->loop, 100, 0, NULL);
        if (srv->retry != DINO_INVALID_EVENT_TIMER)
            dino_event_mod_socket(srv->loop, srv->listener, 0, srv);
    }
}

static void resume_accept(struct server *srv)
{
    dino_event_del_timer(srv->loop, srv->retry);
    srv->retry = DINO_INVALID_EVENT_TIMER;
    dino_event_mod_socket(srv->loop, srv->listener, DINO_EVENT_READ, srv);
}

static void serve_conn(struct server *srv, struct peer *p, uint32_t events)
{
    char buf[256];
    int  n = (int)recv(p->sock, buf, sizeof(buf), 0);
    if (n > 0)
    {
        // A non-blocking send can take less than all of it. These echoes are
        // 16 bytes into a loopback buffer of many kilobytes, so a short one
        // means something is wrong; a real server would keep the rest and
        // watch for DINO_EVENT_WRITE before sending it.
        if (dino_socket_send(p->sock, buf, (size_t)n, 0) != n)
            close_peer(srv, p);
        return;
    }
    // n < 0 without HANGUP/ERROR is just "nothing to read after all".
    if (n == 0 || (events & (DINO_EVENT_HANGUP | DINO_EVENT_ERROR)))
        close_peer(srv, p);
}

static void *run_server(void *arg)
{
    struct server *srv = arg;
    dino_event_t   events[64];
    int            stop = 0;
    while (!stop)
    {
        int n = dino_event_wait(srv->loop, events, 64, -1);
        if (n < 0)
            break;
        // Finish the batch even after a wake: the events after it are for
        // connections that still need closing.
        for (int i = 0; i < n; i++)
        {
            if (events[i].events & DINO_EVENT_WAKE)
                stop = 1;
            else if (events[i].events & DINO_EVENT_TIMER)
                resume_accept(srv);
            else if (events[i].userdata == srv)
                serve_accept(srv);
            else
                serve_conn(srv, events[i].userdata, events[i].events);
        }
    }
    // Whatever is still open is deleted from the loop and closed, once each.
    while (srv->peers)
        close_peer(srv, srv->peers);
    if (srv->retry != DINO_INVALID_EVENT_TIMER)
        dino_event_del_timer(srv->loop, srv->retry);
    return NULL;
}

static struct sockaddr_in loopback(int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

// Port 0: the system picks a free port; bound_port() says which.
static dino_socket_t listen_on(int port)
{
    dino_socket_t s = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (s == DINO_INVALID_SOCKET)
        return s;
    dino_socket_set_reuseaddr(s, 1);
    struct sockaddr_in addr = loopback(port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 128) != 0
        || dino_socket_set_nonblocking(s, 1) != 0)
    {
        printf(
            "[Server] Can't listen: %s (FAILED)\n", dino_error_message(dino_socket_last_error()));
        dino_socket_close(s);
        return DINO_INVALID_SOCKET;
    }
    return s;
}

static int bound_port(dino_socket_t s)
{
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    return getsockname(s, (struct sockaddr *)&addr, &len) == 0 ? ntohs(addr.sin_port) : -1;
}

// Returns how many replies matched what was sent.
static int run_clients(dino_event_loop_t loop, int port)
{
    struct sockaddr_in server_addr = loopback(port);
    for (int i = 0; i < CONNECTIONS; i++)
        conns[i].sock = DINO_INVALID_SOCKET;

    int opened = 0;
    for (int i = 0; i < CONNECTIONS; i++)
    {
        struct conn *c = &conns[i];
        c->sock        = dino_socket_create(AF_INET, SOCK_STREAM, 0);
        if (c->sock == DINO_INVALID_SOCKET
            || dino_socket_connect(c->sock, (struct sockaddr *)&server_addr, sizeof(server_addr))
                != 0)
        {
            printf("[Client] connection %d failed (FAILED)\n", i);
            break;
        }
        snprintf(c->sent, sizeof(c->sent), "conn %04d ping!", i);
        dino_socket_set_nonblocking(c->sock, 1);
        if (dino_event_add_socket(loop, c->sock, DINO_EVENT_READ, c) != 0
            || dino_socket_send(c->sock, c->sent, MSG_LEN, 0) != MSG_LEN)
        {
            printf("[Client] connection %d couldn't send (FAILED)\n", i);
            break;
        }
        opened++;
    }
    printf("[Client] %d connections open\n", opened);

    // Without it a lost reply would leave the wait below blocked forever.
    dino_event_timer_t deadline = dino_event_add_timer(loop, DEADLINE_MS, 0, NULL);
    if (deadline == DINO_INVALID_EVENT_TIMER)
    {
        printf("[Client] can't set the deadline (FAILED)\n");
        opened = 0;
    }
    int          done = 0, matched = 0;
    dino_event_t events[64];
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
            int          r = (int)recv(c->sock, c->got + c->len, MSG_LEN - c->len, 0);
            if (r > 0)
                c->len += r;
            if (c->len < MSG_LEN && r != 0)
                continue;
            matched += c->len == MSG_LEN && memcmp(c->got, c->sent, MSG_LEN) == 0;
            done++;
            dino_event_del_socket(loop, c->sock);
        }
    }
    if (deadline != DINO_INVALID_EVENT_TIMER)
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
    srv.listener      = listen_on(0);
    if (srv.loop == DINO_INVALID_EVENT_LOOP || srv.listener == DINO_INVALID_SOCKET
        || dino_event_add_socket(srv.loop, srv.listener, DINO_EVENT_READ, &srv) != 0)
    {
        printf("[Server] setup failed (FAILED)\n");
        return 1;
    }
    int port = bound_port(srv.listener);
    printf("[Server] Listening on port %d\n", port);

    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, run_server, &srv) != 0)
        return 1;

    dino_event_loop_t loop    = dino_event_loop_create();
    uint64_t          start   = dino_time_monotonic_ns();
    int               matched = loop == DINO_INVALID_EVENT_LOOP ? 0 : run_clients(loop, port);
    uint64_t          ms      = (dino_time_monotonic_ns() - start) / 1000000;

    dino_event_wake(srv.loop);
    pthread_join(server_thread, NULL);
    printf("[Server] peak of %d connections at once\n", srv.peak);
    printf("[Client] echoed %d/%d in %llu ms%s\n", matched, CONNECTIONS, (unsigned long long)ms,
        matched == CONNECTIONS ? "" : " (FAILED)");

    dino_event_loop_destroy(loop);
    dino_event_del_socket(srv.loop, srv.listener);
    dino_socket_close(srv.listener);
    dino_event_loop_destroy(srv.loop);
    return matched == CONNECTIONS ? 0 : 1;
}
