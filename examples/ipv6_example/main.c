#include <stdio.h>
#include <string.h>

#include <dino/core/network.h>

#define PORT 8090

// Server and client in one process: connect() completes against the listen
// backlog before accept(), so a blocking echo needs no threads.
static int echo(dino_socket_domain_t domain, const char *ip)
{
    dino_socket_t server = dino_socket_create(domain, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t client = dino_socket_create(domain, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t peer   = DINO_INVALID_SOCKET;
    char          buf[8] = { 0 };
    int           ok     = 0;

    if (server == DINO_INVALID_SOCKET || client == DINO_INVALID_SOCKET)
        goto done;
    dino_socket_set_reuseaddr(server, 1);
    if (dino_socket_bind(server, ip, PORT) != 0 || dino_socket_listen(server, 1) != 0)
        goto done;
    if (dino_socket_connect(client, ip, PORT) != 0)
        goto done;
    peer = dino_socket_accept(server);
    if (peer == DINO_INVALID_SOCKET)
        goto done;

    if (dino_socket_send(client, "ping", 4) != 4 || dino_socket_recv(peer, buf, 4) != 4)
        goto done;
    if (dino_socket_send(peer, buf, 4) != 4)
        goto done;
    memset(buf, 0, sizeof(buf));
    ok = dino_socket_recv(client, buf, 4) == 4 && strcmp(buf, "ping") == 0;

done:
    printf("echo over %-9s %s\n", ip, ok ? "ok" : "FAILED");
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
    return ok;
}

// One IPv6 listener on "::" for both families. IPv6 sockets start v6-only on
// every platform; set_v6only(0) makes this one take the IPv4 client too.
static int dual_stack(void)
{
    dino_socket_t server = dino_socket_create(DINO_AF_INET6, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t client = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t peer   = DINO_INVALID_SOCKET;
    char          buf[8] = { 0 };

    dino_socket_set_reuseaddr(server, 1);
    int ok = dino_socket_set_v6only(server, 0) == 0 && dino_socket_bind(server, "::", PORT + 1) == 0
        && dino_socket_listen(server, 1) == 0
        && dino_socket_connect(client, "127.0.0.1", PORT + 1) == 0;
    if (ok)
        peer = dino_socket_accept(server);
    ok = ok && peer != DINO_INVALID_SOCKET && dino_socket_send(client, "ping", 4) == 4
        && dino_socket_recv(peer, buf, 4) == 4 && strcmp(buf, "ping") == 0;

    printf("IPv4 client, dual-stack listener on :: %s\n", ok ? "ok" : "FAILED");
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);
    return ok;
}

// Strings that aren't address literals must fail rather than become 0.0.0.0.
// Binding port 0 succeeds for any valid address, so only rejecting the string
// can make it fail.
static int rejects(const char *ip)
{
    dino_socket_t bound = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_t conn  = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    int ok = dino_socket_bind(bound, ip, 0) != 0 && dino_socket_connect(conn, ip, PORT) != 0;

    printf("rejects \"%s\" %s\n", ip, ok ? "ok" : "FAILED");
    dino_socket_close(conn);
    dino_socket_close(bound);
    return ok;
}

int main(void)
{
    int ok = echo(DINO_AF_INET, "127.0.0.1");
    ok &= echo(DINO_AF_INET6, "::1");
    ok &= dual_stack();
    ok &= rejects("localhost");
    ok &= rejects("256.0.0.1");
    ok &= rejects("::zz");
    ok &= rejects("");

    return ok ? 0 : 1;
}
