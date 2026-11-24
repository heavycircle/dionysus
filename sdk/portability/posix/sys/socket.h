#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/socket.h>

#include "dino_portability.h"

// Windows sockets are SOCKET handles, need WSAStartup and close with closesocket.
#if defined(DINO__WARNINGS) && !defined(DINO__SYS_SOCKET_H) && (defined(_SYS_SOCKET_H))
#define DINO__SYS_SOCKET_H

__typeof__(socket) socket __attribute__((deprecated(
    "socket() returns a SOCKET and needs WSAStartup on Windows; use dino_socket_create() from <dino/core/network.h>")));
__typeof__(accept) accept __attribute__((deprecated(
    "accept() returns a SOCKET on Windows; use dino_socket_accept() from <dino/core/network.h>")));
__typeof__(connect) connect __attribute__((deprecated(
    "connect() reports errors through WSAGetLastError on Windows; use dino_socket_connect() from <dino/core/network.h>")));
__typeof__(send) send __attribute__((deprecated(
    "send() raises SIGPIPE on Linux and takes a SOCKET on Windows; use dino_socket_send() from <dino/core/network.h>")));

__typeof__(recvfrom) recvfrom __attribute__((deprecated(
    "recvfrom() needs WSAStartup on Windows; use dino_socket_recvfrom() from <dino/core/network.h>")));
#ifdef _GNU_SOURCE
__typeof__(accept4) accept4 __attribute__((deprecated(
    "accept4() is Linux-only; use dino_socket_accept() from <dino/core/network.h>")));
#endif

#endif
