#ifndef DINO_TESTS_NET_HELPERS_H
#define DINO_TESTS_NET_HELPERS_H

// What a program would write around the POSIX socket calls: an address from
// an IP literal and a port, and the calls that take one. The same code on
// every target, through the SDK's socket headers on Windows.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include <dino/core/network.h>

// The sockaddr for ip and port; its length, or 0 if ip isn't a literal.
static inline socklen_t addr_of(struct sockaddr_storage *ss, const char *ip, int port)
{
    memset(ss, 0, sizeof(*ss));
    struct sockaddr_in  *v4 = (struct sockaddr_in *)ss;
    struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)ss;
    if (inet_pton(AF_INET, ip, &v4->sin_addr) == 1)
    {
        v4->sin_family = AF_INET;
        v4->sin_port   = htons((unsigned short)port);
        return (socklen_t)sizeof(*v4);
    }
    if (inet_pton(AF_INET6, ip, &v6->sin6_addr) == 1)
    {
        v6->sin6_family = AF_INET6;
        v6->sin6_port   = htons((unsigned short)port);
        return (socklen_t)sizeof(*v6);
    }
    return 0;
}

static inline int bind_to(dino_socket_t s, const char *ip, int port)
{
    struct sockaddr_storage ss;
    socklen_t               len = addr_of(&ss, ip, port);
    return len ? bind(s, (struct sockaddr *)&ss, len) : -1;
}

static inline int connect_to(dino_socket_t s, const char *ip, int port)
{
    struct sockaddr_storage ss;
    socklen_t               len = addr_of(&ss, ip, port);
    return len ? dino_socket_connect(s, (struct sockaddr *)&ss, len) : -1;
}

static inline int sendto_ip(dino_socket_t s, const void *buf, int len, const char *ip, int port)
{
    struct sockaddr_storage ss;
    socklen_t               alen = addr_of(&ss, ip, port);
    return alen ? (int)sendto(s, (const char *)buf, len, 0, (struct sockaddr *)&ss, alen) : -1;
}

static inline int recv_some(dino_socket_t s, void *buf, int len)
{
    return (int)recv(s, (char *)buf, len, 0);
}

// The port a socket is bound to, or -1.
static inline int local_port(dino_socket_t s)
{
    struct sockaddr_storage ss;
    socklen_t               len = sizeof(ss);
    if (getsockname(s, (struct sockaddr *)&ss, &len) != 0)
        return -1;
    return ss.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&ss)->sin6_port)
                                    : ntohs(((struct sockaddr_in *)&ss)->sin_port);
}

static inline int set_int_option(dino_socket_t s, int level, int name, int value)
{
    return setsockopt(s, level, name, (const char *)&value, sizeof(value));
}

#endif
