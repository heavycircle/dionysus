#include <dino/core/io.h>
#include <dino/core/network.h>
#include <stdio.h>
#include <string.h>

#define PORT        8081
#define BUFFER_SIZE 1024
#define TIMEOUT_MS  5000
#define IDLE_MS     50

static dino_socket_t listen_on(int port)
{
    dino_socket_t sock = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (sock == DINO_INVALID_SOCKET)
        return DINO_INVALID_SOCKET;

    dino_socket_set_reuseaddr(sock, 1);
    if (dino_socket_bind(sock, "0.0.0.0", port) != 0 || dino_socket_listen(sock, 5) != 0)
    {
        dino_socket_close(sock);
        return DINO_INVALID_SOCKET;
    }
    return sock;
}

static dino_socket_t connect_to(int port)
{
    dino_socket_t sock = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (sock != DINO_INVALID_SOCKET && dino_socket_connect(sock, "127.0.0.1", port) != 0)
    {
        dino_socket_close(sock);
        return DINO_INVALID_SOCKET;
    }
    return sock;
}

// A listening socket polls readable when a connection is waiting to be accepted.
static int wait_readable(const char *who, dino_socket_t sock, int timeout_ms)
{
    dino_pollfd_t pfd = { sock, DINO_POLLIN, 0 };
    int           ok  = dino_poll(&pfd, 1, timeout_ms) == 1 && (pfd.revents & DINO_POLLIN);
    if (!ok)
        printf("[%s] Not readable within %dms (FAILED)\n", who, timeout_ms);
    return ok;
}

static int send_msg(const char *who, dino_socket_t sock, const char *msg)
{
    int ok = dino_socket_send(sock, msg, (int)strlen(msg)) == (int)strlen(msg);
    printf("[%s] Sent: %s%s\n", who, msg, ok ? "" : " (FAILED)");
    return ok;
}

static int expect_msg(const char *who, dino_socket_t sock, const char *expected)
{
    if (!wait_readable(who, sock, TIMEOUT_MS))
        return 0;

    char buf[BUFFER_SIZE] = { 0 };
    int  n                = dino_socket_recv(sock, buf, BUFFER_SIZE - 1);
    int  ok               = n > 0 && strcmp(buf, expected) == 0;
    printf("[%s] Received: %s%s\n", who, n > 0 ? buf : "(nothing)", ok ? "" : " (FAILED)");
    return ok;
}

static int run_server(void)
{
    dino_socket_t server = listen_on(PORT);
    if (server == DINO_INVALID_SOCKET)
        return 0;
    printf("[Server] Listening on port %d (poll-based, %dms timeout)\n", PORT, TIMEOUT_MS);

    dino_socket_t client = wait_readable("Server", server, TIMEOUT_MS) ? dino_socket_accept(server)
                                                                       : DINO_INVALID_SOCKET;
    int           ok     = client != DINO_INVALID_SOCKET && expect_msg("Server", client, "Ping!")
        && send_msg("Server", client, "Pong!");

    if (client != DINO_INVALID_SOCKET)
    {
        dino_socket_shutdown(client, DINO_SHUT_RDWR);
        dino_socket_close(client);
    }
    dino_socket_close(server);
    return ok;
}

static int run_client(void)
{
    dino_socket_t sock = connect_to(PORT);
    if (sock == DINO_INVALID_SOCKET)
    {
        fprintf(stderr, "[Client] Connection failed\n");
        return 0;
    }

    int ok = send_msg("Client", sock, "Ping!") && expect_msg("Client", sock, "Pong!");
    dino_socket_close(sock);
    return ok;
}

// Both ends in one process, every receive gated by dino_poll. Also checks that
// an idle socket times out instead of reporting readiness.
static int run_self_test(void)
{
    dino_socket_t server = listen_on(PORT);
    dino_socket_t client = connect_to(PORT);
    dino_socket_t peer   = DINO_INVALID_SOCKET;
    int           ok     = 0;

    if (server == DINO_INVALID_SOCKET || client == DINO_INVALID_SOCKET)
        goto done;
    if (!wait_readable("Server", server, TIMEOUT_MS))
        goto done;
    peer = dino_socket_accept(server);
    if (peer == DINO_INVALID_SOCKET)
        goto done;

    dino_pollfd_t idle = { peer, DINO_POLLIN, 0 };
    if (dino_poll(&idle, 1, IDLE_MS) != 0)
    {
        puts("[Server] Idle socket reported ready (FAILED)");
        goto done;
    }
    printf("[Server] Idle socket timed out after %dms\n", IDLE_MS);

    ok = send_msg("Client", client, "Ping!") && expect_msg("Server", peer, "Ping!")
        && send_msg("Server", peer, "Pong!") && expect_msg("Client", client, "Pong!");

done:
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    if (client != DINO_INVALID_SOCKET)
        dino_socket_close(client);
    if (server != DINO_INVALID_SOCKET)
        dino_socket_close(server);
    return ok;
}

int main(int argc, char *argv[])
{
    if (dino_net_init() != 0)
        return 1;

    int ok;
    if (argc > 1 && strcmp(argv[1], "server") == 0)
        ok = run_server();
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
        ok = run_client();
    else
        ok = run_self_test();

    dino_net_cleanup();
    return ok ? 0 : 1;
}
