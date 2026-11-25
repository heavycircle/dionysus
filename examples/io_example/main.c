#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/error.h>
#include <dino/core/network.h>

/* Waiting on sockets with plain poll(), on every target: the SDK provides it
   on Windows, over WSAPoll, which waits on sockets only. */

// The port of the two-terminal mode; the self-test lets the system pick one.
#define PORT        8081
#define BUFFER_SIZE 1024
#define TIMEOUT_MS  5000
#define IDLE_MS     50

static struct sockaddr_in loopback(int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

// Port 0 lets the system pick a free port; bound_port() says which.
static dino_socket_t listen_on(int port)
{
    dino_socket_t sock = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (sock == DINO_INVALID_SOCKET)
        return DINO_INVALID_SOCKET;

    dino_socket_set_reuseaddr(sock, 1);
    struct sockaddr_in addr = loopback(port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(sock, 5) != 0)
    {
        printf("[Server] Can't listen on port %d: %s (FAILED)\n", port,
            dino_error_message(dino_socket_last_error()));
        dino_socket_close(sock);
        return DINO_INVALID_SOCKET;
    }
    return sock;
}

static int bound_port(dino_socket_t sock)
{
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &len) != 0)
        return -1;
    return ntohs(addr.sin_port);
}

static dino_socket_t connect_to(int port)
{
    dino_socket_t      sock = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = loopback(port);
    if (sock != DINO_INVALID_SOCKET
        && dino_socket_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        dino_socket_close(sock);
        return DINO_INVALID_SOCKET;
    }
    return sock;
}

// A listening socket polls readable when a connection is waiting to be accepted.
static int wait_readable(const char *who, dino_socket_t sock, int timeout_ms)
{
    struct pollfd pfd = { sock, POLLIN, 0 };
    int           ok  = poll(&pfd, 1, timeout_ms) == 1 && (pfd.revents & POLLIN);
    if (!ok)
        printf("[%s] Not readable within %dms (FAILED)\n", who, timeout_ms);
    return ok;
}

static int send_msg(const char *who, dino_socket_t sock, const char *msg)
{
    int ok = dino_socket_send(sock, msg, strlen(msg), 0) == (int)strlen(msg);
    printf("[%s] Sent: %s%s\n", who, msg, ok ? "" : " (FAILED)");
    return ok;
}

static int expect_msg(const char *who, dino_socket_t sock, const char *expected)
{
    if (!wait_readable(who, sock, TIMEOUT_MS))
        return 0;

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
    printf("[Server] Listening on port %d; start the client now\n", PORT);

    // However long the user takes to start the client: poll in rounds, and
    // keep the time limit for the exchange itself.
    struct pollfd pfd = { server, POLLIN, 0 };
    int           ready;
    while ((ready = poll(&pfd, 1, TIMEOUT_MS)) == 0)
        printf("[Server] Still waiting for the client...\n");
    dino_socket_t client
        = ready == 1 ? dino_socket_accept(server, NULL, NULL) : DINO_INVALID_SOCKET;
    int ok = client != DINO_INVALID_SOCKET && expect_msg("Server", client, "Ping!")
        && send_msg("Server", client, "Pong!");

    if (client != DINO_INVALID_SOCKET)
    {
        shutdown(client, SHUT_RDWR);
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

static int check_writable(dino_socket_t sock)
{
    struct pollfd pfd = { sock, POLLOUT, 0 };
    int           ok  = poll(&pfd, 1, TIMEOUT_MS) == 1 && (pfd.revents & POLLOUT);
    printf("[Client] Writable%s\n", ok ? "" : " (FAILED)");
    return ok;
}

// Closes *closing and expects its peer to report it. Linux signals a closed
// peer as readable, Windows as POLLHUP; either way recv() returns 0.
static int check_hangup(dino_socket_t sock, dino_socket_t *closing)
{
    dino_socket_close(*closing);
    *closing = DINO_INVALID_SOCKET;

    char          buf[1];
    struct pollfd pfd = { sock, POLLIN, 0 };
    int           ok  = poll(&pfd, 1, TIMEOUT_MS) == 1 && (pfd.revents & (POLLIN | POLLHUP))
        && (int)recv(sock, buf, sizeof(buf), 0) == 0;
    printf("[Server] Peer hang-up seen (revents 0x%x)%s\n", pfd.revents, ok ? "" : " (FAILED)");
    return ok;
}

// Both ends in one process, every receive gated by poll. Also checks that
// an idle socket times out, a connected socket polls writable, and a closed
// peer is reported.
static int run_self_test(void)
{
    dino_socket_t server = listen_on(0);
    dino_socket_t client
        = server == DINO_INVALID_SOCKET ? DINO_INVALID_SOCKET : connect_to(bound_port(server));
    dino_socket_t peer = DINO_INVALID_SOCKET;
    int           ok   = 0;

    if (server == DINO_INVALID_SOCKET)
        goto done;
    if (client == DINO_INVALID_SOCKET)
    {
        printf("[Client] Can't connect: %s (FAILED)\n", dino_error_message(dino_error()));
        goto done;
    }
    if (!wait_readable("Server", server, TIMEOUT_MS))
        goto done;
    peer = dino_socket_accept(server, NULL, NULL);
    if (peer == DINO_INVALID_SOCKET)
        goto done;

    struct pollfd idle = { peer, POLLIN, 0 };
    if (poll(&idle, 1, IDLE_MS) != 0)
    {
        puts("[Server] Idle socket reported ready (FAILED)");
        goto done;
    }
    printf("[Server] Idle socket timed out after %dms\n", IDLE_MS);

    ok = send_msg("Client", client, "Ping!") && expect_msg("Server", peer, "Ping!")
        && send_msg("Server", peer, "Pong!") && expect_msg("Client", client, "Pong!")
        && check_writable(client) && check_hangup(peer, &client);

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
    int ok;
    if (argc > 1 && strcmp(argv[1], "server") == 0)
        ok = run_server();
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
        ok = run_client();
    else
        ok = run_self_test();

    return ok ? 0 : 1;
}
