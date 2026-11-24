#include "server.h"

#include <string.h>
#include <sys/socket.h>

#include "protocol.h"

// One byte at a time keeps line framing trivial; requests are short.
static int read_line(dino_socket_t sock, char *buf, size_t len)
{
    size_t n = 0;
    for (;;)
    {
        char c;
        int  got = (int)recv(sock, &c, 1, 0);
        if (got <= 0)
            return got == 0 && n == 0 ? 1 : -1;
        if (c == '\n')
            break;
        if (c != '\r' && n + 1 < len)
            buf[n++] = c;
    }
    buf[n] = '\0';
    return 0;
}

int kv_serve_one(struct kv *db, dino_socket_t sock)
{
    char request[KV_LINE_MAX];
    int  rc = read_line(sock, request, sizeof(request));
    if (rc != 0)
        return rc;

    char response[KV_LINE_MAX + 1];
    kv_execute(db, request, response, KV_LINE_MAX);
    size_t len      = strlen(response);
    response[len++] = '\n';
    return dino_socket_send(sock, response, len, 0) == (int)len ? 0 : -1;
}
