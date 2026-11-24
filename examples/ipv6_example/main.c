#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/network.h>

#define PORT 8090

/* IPv4 and IPv6 with the same code: getaddrinfo picks the family and fills in
   the address, on every target. */

static struct addrinfo *numeric(const char *ip, int port)
{
    char service[8];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_NUMERICHOST;
    return getaddrinfo(ip, service, &hints, &res) == 0 ? res : NULL;
}

// Server and client in one process: connect() completes against the listen
// backlog before accept(), so a blocking echo needs no threads.
static int echo(const char *ip)
{
    struct addrinfo *addr   = numeric(ip, PORT);
    dino_socket_t    server = DINO_INVALID_SOCKET, client = DINO_INVALID_SOCKET;
    dino_socket_t    peer   = DINO_INVALID_SOCKET;
    char             buf[8] = { 0 };
    int              ok     = 0;

    if (!addr)
        goto done;
    server = dino_socket_create(addr->ai_family, SOCK_STREAM, 0);
    client = dino_socket_create(addr->ai_family, SOCK_STREAM, 0);
    if (server == DINO_INVALID_SOCKET || client == DINO_INVALID_SOCKET)
        goto done;
    dino_socket_set_reuseaddr(server, 1);
    if (bind(server, addr->ai_addr, addr->ai_addrlen) != 0 || listen(server, 1) != 0)
        goto done;
    if (dino_socket_connect(client, addr->ai_addr, addr->ai_addrlen) != 0)
        goto done;
    peer = dino_socket_accept(server, NULL, NULL);
    if (peer == DINO_INVALID_SOCKET)
        goto done;

    if (dino_socket_send(client, "ping", 4, 0) != 4 || recv(peer, buf, 4, 0) != 4)
        goto done;
    if (dino_socket_send(peer, buf, 4, 0) != 4)
        goto done;
    memset(buf, 0, sizeof(buf));
    ok = recv(client, buf, 4, 0) == 4 && strcmp(buf, "ping") == 0;

done:
    printf("echo over %-9s %s\n", ip, ok ? "ok" : "FAILED");
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    if (client != DINO_INVALID_SOCKET)
        dino_socket_close(client);
    if (server != DINO_INVALID_SOCKET)
        dino_socket_close(server);
    if (addr)
        freeaddrinfo(addr);
    return ok;
}

// One IPv6 listener on "::" for both families. IPv6 sockets start v6-only on
// every platform; clearing IPV6_V6ONLY makes this one take the IPv4 client too.
static int dual_stack(void)
{
    struct addrinfo *any = numeric("::", PORT + 1), *v4 = numeric("127.0.0.1", PORT + 1);
    dino_socket_t    server = dino_socket_create(AF_INET6, SOCK_STREAM, 0);
    dino_socket_t    client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    dino_socket_t    peer   = DINO_INVALID_SOCKET;
    char             buf[8] = { 0 };
    int              off    = 0;

    dino_socket_set_reuseaddr(server, 1);
    int ok = any && v4
        && setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof(off)) == 0
        && bind(server, any->ai_addr, any->ai_addrlen) == 0 && listen(server, 1) == 0
        && dino_socket_connect(client, v4->ai_addr, v4->ai_addrlen) == 0;
    if (ok)
        peer = dino_socket_accept(server, NULL, NULL);
    ok = ok && peer != DINO_INVALID_SOCKET && dino_socket_send(client, "ping", 4, 0) == 4
        && recv(peer, buf, 4, 0) == 4 && strcmp(buf, "ping") == 0;

    printf("IPv4 client, dual-stack listener on :: %s\n", ok ? "ok" : "FAILED");
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
    if (any)
        freeaddrinfo(any);
    if (v4)
        freeaddrinfo(v4);
    return ok;
}

int main(void)
{
    int ok = echo("127.0.0.1");
    ok &= echo("::1");
    ok &= dual_stack();
    return ok ? 0 : 1;
}
