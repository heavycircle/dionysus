#include <dino/core/network.h>
#include <dino/core/runtime.h>
#include <stdio.h>
#include <string.h>

/* Subsystems start up before main() and tear down after — no explicit
   dino_net_init() or dino_net_cleanup() calls needed anywhere. */
DINO_ENABLE_AUTO_RUNTIME;

#define PORT        8082
#define BUFFER_SIZE 1024

void run_server(void)
{
    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (server == DINO_INVALID_SOCKET)
        return;

    dino_socket_set_reuseaddr(server, 1);
    dino_socket_bind(server, "0.0.0.0", PORT);
    dino_socket_listen(server, 5);
    printf("[Server] Listening on port %d\n", PORT);

    dino_socket_t client = dino_socket_accept(server);
    puts("[Server] Client connected!");

    char buf[BUFFER_SIZE] = { 0 };
    int  n                = dino_socket_recv(client, buf, BUFFER_SIZE - 1);
    if (n > 0)
    {
        printf("[Server] Received: %s\n", buf);
        const char *resp = "Pong!";
        dino_socket_send(client, resp, strlen(resp));
        puts("[Server] Response sent.");
    }

    dino_socket_shutdown(client, DINO_SHUT_RDWR);
    dino_socket_close(client);
    dino_socket_close(server);
}

void run_client(void)
{
    dino_socket_t sock = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (sock == DINO_INVALID_SOCKET)
        return;

    if (dino_socket_connect(sock, "127.0.0.1", PORT) != 0)
    {
        fprintf(stderr, "[Client] Connection failed\n");
        dino_socket_close(sock);
        return;
    }

    const char *msg = "Ping!";
    dino_socket_send(sock, msg, strlen(msg));
    puts("[Client] Sent message!");

    char buf[BUFFER_SIZE] = { 0 };
    int  n                = dino_socket_recv(sock, buf, BUFFER_SIZE - 1);
    if (n > 0)
        printf("[Client] Response: %s\n", buf);

    dino_socket_close(sock);
}

int main(int argc, char *argv[])
{
    if (argc > 1 && strcmp(argv[1], "server") == 0)
        run_server();
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
        run_client();
    else
        printf("Usage: %s [server|client]\n", argv[0]);
    return 0;
}
