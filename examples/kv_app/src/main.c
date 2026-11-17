#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dino/core/network.h>

#include "kv.h"
#include "server.h"

#define DEMO_PORT 8095

static int serve(int port)
{
    struct kv    *db       = kv_create();
    dino_socket_t listener = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (!db || listener == DINO_INVALID_SOCKET)
        return 1;
    dino_socket_set_reuseaddr(listener, 1);
    if (dino_socket_bind(listener, "0.0.0.0", port) != 0 || dino_socket_listen(listener, 8) != 0)
    {
        fprintf(stderr, "cannot listen on port %d\n", port);
        return 1;
    }
    printf("kv_app listening on port %d (try: printf 'SET a 1\\nGET a\\n' | nc localhost %d)\n",
        port, port);

    for (;;)
    {
        dino_socket_t client = dino_socket_accept(listener);
        if (client == DINO_INVALID_SOCKET)
            continue;
        while (kv_serve_one(db, client) == 0)
            ;
        dino_socket_close(client);
    }
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
    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(server, 1);
    if (!db || dino_socket_bind(server, "127.0.0.1", DEMO_PORT) != 0
        || dino_socket_listen(server, 1) != 0
        || dino_socket_connect(client, "127.0.0.1", DEMO_PORT) != 0)
        return 1;
    dino_socket_t peer = dino_socket_accept(server);

    int ok = peer != DINO_INVALID_SOCKET;
    for (size_t i = 0; ok && i < sizeof(script) / sizeof(script[0]); i++)
    {
        char request[300], response[300] = { 0 };
        int  len = snprintf(request, sizeof(request), "%s\n", script[i][0]);
        // The client's request sits in the socket buffer until the server reads it.
        ok = dino_socket_send(client, request, len) == len && kv_serve_one(db, peer) == 0
            && dino_socket_recv(client, response, sizeof(response) - 1) > 0;
        response[strcspn(response, "\n")] = '\0';
        ok                                = ok && strcmp(response, script[i][1]) == 0;
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
