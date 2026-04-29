#include <dino/core/network.h>
#include <stdio.h>
#include <string.h>

#define PORT        8080
#define BUFFER_SIZE 1024

void run_server(void)
{
    dino_socket_t server_sock
        = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (server_sock == DINO_INVALID_SOCKET)
        return;

    dino_socket_bind(server_sock, "0.0.0.0", PORT);
    dino_socket_listen(server_sock, 5);

    dino_socket_t client_sock = dino_socket_accept(server_sock);
    puts("[Server] Client connected!");

    char buffer[BUFFER_SIZE] = { 0 };
    int  bytes               = dino_socket_recv(client_sock, buffer, BUFFER_SIZE - 1);
    if (bytes > 0)
    {
        printf("[Server] Received: %s\n", buffer);

        const char *response = "Pong!";
        dino_socket_send(client_sock, response, strlen(response));
        puts("[Server] Response sent.");
    }

    dino_socket_close(client_sock);
    dino_socket_close(server_sock);
}

void run_client(void)
{
    dino_socket_t client_sock
        = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (client_sock == DINO_INVALID_SOCKET)
        return;

    if (dino_socket_connect(client_sock, "127.0.0.1", PORT) != 0)
    {
        fprintf(stderr, "Connection failed!\n");
        dino_socket_close(client_sock);
        return;
    }

    const char *msg = "Ping!";
    dino_socket_send(client_sock, msg, strlen(msg));
    puts("[Client] Sent message!");

    char buffer[BUFFER_SIZE] = { 0 };
    int  bytes               = dino_socket_recv(client_sock, buffer, BUFFER_SIZE - 1);
    if (bytes > 0)
        printf("[Client] Server Response: %s\n", buffer);

    dino_socket_close(client_sock);
}

int main(int argc, char *argv[])
{
    dino_net_init();

    if (argc > 1 && strcmp(argv[1], "server") == 0)
        run_server();
    else if (argc > 1 && strcmp(argv[1], "client") == 0)
        run_client();
    else
        printf("Usage: %s [server|client]\n", argv[0]);

    dino_net_cleanup();
    return 0;
}
