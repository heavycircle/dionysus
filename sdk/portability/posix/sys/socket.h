#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/socket.h>

#include "dino_portability.h"

// Windows sockets are SOCKET handles, need WSAStartup and close with closesocket.
#if defined(DINO__WARNINGS) && !defined(DINO__SYS_SOCKET_H) && (defined(_SYS_SOCKET_H))
#define DINO__SYS_SOCKET_H

__typeof__(socket) socket __attribute__((deprecated(
    "socket() returns a SOCKET and needs WSAStartup on Windows; use dino_socket_create() from <dino/core/network.h>")));
__typeof__(bind) bind __attribute__((deprecated(
    "bind() takes a SOCKET on Windows; use dino_socket_bind() from <dino/core/network.h>")));
__typeof__(listen) listen __attribute__((deprecated(
    "listen() takes a SOCKET on Windows; use dino_socket_listen() from <dino/core/network.h>")));
__typeof__(accept) accept __attribute__((deprecated(
    "accept() returns a SOCKET on Windows; use dino_socket_accept() from <dino/core/network.h>")));
__typeof__(connect) connect __attribute__((deprecated(
    "connect() reports errors through WSAGetLastError on Windows; use dino_socket_connect() from <dino/core/network.h>")));
__typeof__(send) send __attribute__((deprecated(
    "send() raises SIGPIPE on Linux and takes a SOCKET on Windows; use dino_socket_send() from <dino/core/network.h>")));
__typeof__(recv) recv __attribute__((deprecated(
    "recv() takes a SOCKET on Windows; use dino_socket_recv() from <dino/core/network.h>")));
__typeof__(setsockopt) setsockopt __attribute__((deprecated(
    "setsockopt() takes a const char * value on Windows; use dino_socket_setopt() from <dino/core/network.h>, which passes the option on as given (SO_RCVTIMEO and SO_LINGER still take different types per platform)")));
__typeof__(shutdown) shutdown __attribute__((deprecated(
    "shutdown() uses different constants on Windows; use dino_socket_shutdown() from <dino/core/network.h>")));

__typeof__(sendto) sendto __attribute__((deprecated(
    "sendto() needs WSAStartup on Windows; use dino_socket_sendto() from <dino/core/network.h>")));
__typeof__(recvfrom) recvfrom __attribute__((deprecated(
    "recvfrom() needs WSAStartup on Windows; use dino_socket_recvfrom() from <dino/core/network.h>")));
#ifdef _GNU_SOURCE
__typeof__(accept4) accept4 __attribute__((deprecated(
    "accept4() is Linux-only; use dino_socket_accept() from <dino/core/network.h>")));
#endif

#endif
