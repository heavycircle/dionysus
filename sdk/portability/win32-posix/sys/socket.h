#pragma GCC system_header
// POSIX socket declarations on Windows, where MinGW ships no <sys/socket.h>:
// Winsock under the POSIX header names, so the same includes work on every
// target. A SOCKET is still not a file descriptor: create, close, accept,
// connect, send and recvfrom go through dino/core/network.h.
#ifndef DINO_WIN32_SYS_SOCKET_H
#define DINO_WIN32_SYS_SOCKET_H

#include <winsock2.h>
#include <ws2tcpip.h>

// shutdown()'s directions under their POSIX names; Winsock's SD_* have the
// same values.
#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

// Linux-only, and Windows never raises SIGPIPE: nothing to ask for.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// Winsock must be started before any socket call, getaddrinfo included.
// Referencing this links the SDK's constructor that starts it before main()
// (sdk/src/core/winsock_auto_win32.c), only into programs that include a
// socket header.
#ifndef DINO__SDK_SOURCE
#ifdef __cplusplus
extern "C" int dino__winsock_auto;
#else
extern int dino__winsock_auto;
#endif
__attribute__((used)) static int *const dino__winsock_auto_ref = &dino__winsock_auto;
#endif

#endif
