#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/error.h>
#include <dino/core/network.h>
#include <dino/core/time.h>

#include "kv.h"
#include "server.h"

static struct sockaddr_in ipv4(const char *ip, int port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

static int serve(int port)
{
    struct kv    *db       = kv_create();
    dino_socket_t listener = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (!db || listener == DINO_INVALID_SOCKET)
        return 1;
    dino_socket_set_reuseaddr(listener, 1);
    struct sockaddr_in any = ipv4("0.0.0.0", port);
    if (bind(listener, (struct sockaddr *)&any, sizeof(any)) != 0 || listen(listener, 8) != 0)
    {
        fprintf(stderr, "cannot listen on port %d\n", port);
        return 1;
    }
    printf("kv_app listening on port %d (try: printf 'SET a 1\\nGET a\\n' | nc localhost %d)\n",
        port, port);

    for (;;)
    {
        dino_socket_t client = dino_socket_accept(listener, NULL, NULL);
        if (client == DINO_INVALID_SOCKET)
        {
            // Out of descriptors, say: the connection waits in the backlog, so
            // trying again at once would only spin.
            fprintf(stderr, "accept failed: %s\n", dino_error_message(dino_error()));
            dino_sleep_ms(100);
            continue;
        }
        while (kv_serve_one(db, client) == 0)
            ;
        dino_socket_close(client);
    }
}

// Reads one response line, however many recv calls it arrives in: a stream
// socket keeps no message boundaries. Returns its length without the newline,
// or -1.
static int recv_line(dino_socket_t sock, char *buf, int size)
{
    int n = 0;
    while (n < size - 1)
    {
        int got = (int)recv(sock, buf + n, 1, 0);
        if (got <= 0)
            return -1;
        if (buf[n] == '\n')
            break;
        n++;
    }
    buf[n] = '\0';
    return n;
}

// With no arguments the program demonstrates itself: a client and the server
// in one process, talking over loopback. tests/examples.sh runs it this way.
static int demo(void)
{
    const char *script[][2] = {
        { "SET greeting hello world", "OK" },
        { "GET greeting", "VALUE hello world" },
        { "COUNT", "COUNT 1" },
        { "DEL greeting", "OK" },
        { "GET greeting", "NOT_FOUND" },
    };

    struct kv    *db     = kv_create();
    dino_socket_t server = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    // Port 0: the system picks a free one, and getsockname says which.
    struct sockaddr_in local = ipv4("127.0.0.1", 0);
    socklen_t          len   = sizeof(local);
    if (!db || bind(server, (struct sockaddr *)&local, sizeof(local)) != 0 || listen(server, 1) != 0
        || getsockname(server, (struct sockaddr *)&local, &len) != 0
        || dino_socket_connect(client, (struct sockaddr *)&local, sizeof(local)) != 0)
    {
        fprintf(stderr, "demo setup failed: %s\n", dino_error_message(dino_socket_last_error()));
        return 1;
    }
    dino_socket_t peer = dino_socket_accept(server, NULL, NULL);

    int ok = peer != DINO_INVALID_SOCKET;
    for (size_t i = 0; ok && i < sizeof(script) / sizeof(script[0]); i++)
    {
        char request[300], response[300] = { 0 };
        int  n = snprintf(request, sizeof(request), "%s\n", script[i][0]);
        // The client's request sits in the socket buffer until the server reads it.
        ok = dino_socket_send(client, request, n, 0) == n && kv_serve_one(db, peer) == 0
            && recv_line(client, response, sizeof(response)) >= 0
            && strcmp(response, script[i][1]) == 0;
        printf("> %-26s < %s%s\n", script[i][0], response, ok ? "" : "  (FAILED)");
    }

    dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
    kv_destroy(db);
    return ok ? 0 : 1;
}

int main(int argc, char *argv[])
{
    return argc > 1 && strcmp(argv[1], "serve") == 0 ? serve(argc > 2 ? atoi(argv[2]) : 7070)
                                                     : demo();
}
