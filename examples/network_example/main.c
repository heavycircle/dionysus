#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/network.h>

/* Sockets are the POSIX calls on every target: getaddrinfo, bind, listen,
   recv. The SDK provides the headers on Windows, and dino_socket_* for the
   calls the platforms can't agree on: create, close, accept, connect, send. */

#define PORT        8080
#define BUFFER_SIZE 1024

// The first address getaddrinfo gives for host and port. Free it with
// freeaddrinfo.
static struct addrinfo *resolve(const char *host, int port, int passive)
{
    char service[8];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = passive ? AI_PASSIVE : 0;
    return getaddrinfo(host, service, &hints, &res) == 0 ? res : NULL;
}

static dino_socket_t listen_on(int port)
{
    struct addrinfo *addr = resolve(NULL, port, 1);
    if (!addr)
        return DINO_INVALID_SOCKET;
    dino_socket_t sock = dino_socket_create(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock != DINO_INVALID_SOCKET)
    {
        dino_socket_set_reuseaddr(sock, 1);
        if (bind(sock, addr->ai_addr, addr->ai_addrlen) != 0 || listen(sock, 5) != 0)
        {
            dino_socket_close(sock);
            sock = DINO_INVALID_SOCKET;
        }
    }
    freeaddrinfo(addr);
    return sock;
}

static dino_socket_t connect_to(int port)
{
    struct addrinfo *addr = resolve("localhost", port, 0);
    if (!addr)
        return DINO_INVALID_SOCKET;
    dino_socket_t sock = dino_socket_create(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock != DINO_INVALID_SOCKET
        && dino_socket_connect(sock, addr->ai_addr, addr->ai_addrlen) != 0)
    {
        dino_socket_close(sock);
        sock = DINO_INVALID_SOCKET;
    }
    freeaddrinfo(addr);
    return sock;
}

static int send_msg(const char *who, dino_socket_t sock, const char *msg)
{
    int ok = dino_socket_send(sock, msg, strlen(msg), 0) == (int)strlen(msg);
    printf("[%s] Sent: %s%s\n", who, msg, ok ? "" : " (FAILED)");
    return ok;
}

static int expect_msg(const char *who, dino_socket_t sock, const char *expected)
{
    char buf[BUFFER_SIZE] = { 0 };
    int  n                = (int)recv(sock, buf, BUFFER_SIZE - 1, 0);
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

    dino_socket_t client = dino_socket_accept(server, NULL, NULL);
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
    dino_socket_t peer   = server != DINO_INVALID_SOCKET ? dino_socket_accept(server, NULL, NULL)
                                                         : DINO_INVALID_SOCKET;

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
