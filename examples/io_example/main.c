#include <dino/core/io.h>
#include <dino/core/network.h>
#include <stdio.h>
#include <string.h>

#define PORT        8081
#define BUFFER_SIZE 1024
#define TIMEOUT_MS  5000

void run_server(void)
{
    dino_net_init();

    dino_socket_t server = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (server == DINO_INVALID_SOCKET)
    {
        fprintf(stderr, "[Server] Failed to create socket\n");
        return;
    }

    dino_socket_set_reuseaddr(server, 1);
    dino_socket_bind(server, "0.0.0.0", PORT);
    dino_socket_listen(server, 5);
    printf("[Server] Listening on port %d (poll-based, %dms timeout)\n", PORT, TIMEOUT_MS);

    dino_pollfd_t pfd = { server, DINO_POLLIN, 0 };
    if (dino_poll(&pfd, 1, TIMEOUT_MS) <= 0)
    {
        puts("[Server] Timed out waiting for connection");
        dino_socket_close(server);
        dino_net_cleanup();
        return;
    }

    dino_socket_t client = dino_socket_accept(server);
    puts("[Server] Client connected!");

    pfd.sock = client;
    if (dino_poll(&pfd, 1, TIMEOUT_MS) > 0 && (pfd.revents & DINO_POLLIN))
    {
        char buf[BUFFER_SIZE] = { 0 };
        int  n                = dino_socket_recv(client, buf, BUFFER_SIZE - 1);
        if (n > 0)
        {
            printf("[Server] Received: %s\n", buf);
            const char *resp = "Pong!";
            dino_socket_send(client, resp, strlen(resp));
            puts("[Server] Response sent.");
        }
    }

    dino_socket_shutdown(client, DINO_SHUT_RDWR);
    dino_socket_close(client);
    dino_socket_close(server);
    dino_net_cleanup();
}

void run_client(void)
{
    dino_net_init();

    dino_socket_t sock = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (sock == DINO_INVALID_SOCKET)
        return;

    if (dino_socket_connect(sock, "127.0.0.1", PORT) != 0)
    {
        fprintf(stderr, "[Client] Connection failed\n");
        dino_socket_close(sock);
        dino_net_cleanup();
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
    dino_net_cleanup();
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
