#include <stdio.h>
#include <string.h>

#include <dino/core/network.h>

#define PORT        8080
#define BUFFER_SIZE 1024

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

static int send_msg(const char *who, dino_socket_t sock, const char *msg)
{
    int ok = dino_socket_send(sock, msg, (int)strlen(msg)) == (int)strlen(msg);
    printf("[%s] Sent: %s%s\n", who, msg, ok ? "" : " (FAILED)");
    return ok;
}

static int expect_msg(const char *who, dino_socket_t sock, const char *expected)
{
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
    printf("[Server] Listening on port %d\n", PORT);

    dino_socket_t client = dino_socket_accept(server);
    int           ok     = client != DINO_INVALID_SOCKET && expect_msg("Server", client, "Ping!")
        && send_msg("Server", client, "Pong!");

    if (client != DINO_INVALID_SOCKET)
        dino_socket_close(client);
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

// Both ends in one process: connect() completes against the listen backlog
// before accept(), so interleaving the steps needs no threads.
static int run_self_test(void)
{
    dino_socket_t server = listen_on(PORT);
    dino_socket_t client = connect_to(PORT);
    dino_socket_t peer
        = server != DINO_INVALID_SOCKET ? dino_socket_accept(server) : DINO_INVALID_SOCKET;

    int ok = server != DINO_INVALID_SOCKET && client != DINO_INVALID_SOCKET
        && peer != DINO_INVALID_SOCKET && send_msg("Client", client, "Ping!")
        && expect_msg("Server", peer, "Ping!") && send_msg("Server", peer, "Pong!")
        && expect_msg("Client", client, "Pong!");

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
    int ok;
    if (argc > 1 && strcmp(argv[1], "server") == 0)
        ok = run_server();
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
        ok = run_client();
    else
        ok = run_self_test();

    return ok ? 0 : 1;
}
