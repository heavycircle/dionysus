#include "server.h"

#include <string.h>
#include <sys/socket.h>

#include "protocol.h"

// One byte at a time keeps line framing trivial; requests are short. A line
// too long for buf is read to its end anyway, so the next one starts fresh.
// Returns 0 for a line, 1 at a clean end of stream, 2 for a line too long,
// -1 on a socket error.
static int read_line(dino_socket_t sock, char *buf, size_t len)
{
    size_t n        = 0;
    int    too_long = 0;
    for (;;)
    {
        char c;
        int  got = (int)recv(sock, &c, 1, 0);
        if (got <= 0)
            return got == 0 && n == 0 && !too_long ? 1 : -1;
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        if (n + 1 < len)
            buf[n++] = c;
        else
            too_long = 1;
    }
    buf[n] = '\0';
    return too_long ? 2 : 0;
}

int kv_serve_one(struct kv *db, dino_socket_t sock)
{
    char request[KV_LINE_MAX];
    int  rc = read_line(sock, request, sizeof(request));
    if (rc == 1 || rc == -1)
        return rc;

    char response[KV_LINE_MAX + 1];
    if (rc == 2)
        strcpy(response, "ERR request too long");
    else
        kv_execute(db, request, response, KV_LINE_MAX);
    size_t len      = strlen(response);
    response[len++] = '\n';
    return dino_socket_send(sock, response, len, 0) == (int)len ? 0 : -1;
}
