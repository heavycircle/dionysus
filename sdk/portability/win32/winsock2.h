#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <winsock2.h>

#include "dino_portability.h"

// WINSOCK_API_LINKAGE keeps the redeclarations dllimport, like the originals.
// INCL_WINSOCK_API_PROTOTYPES 0 leaves the functions undeclared; unset means declared.
#if defined(DINO__WARNINGS) && !defined(DINO__WINSOCK2_H) && (defined(_WINSOCK2API_)) && (!defined(INCL_WINSOCK_API_PROTOTYPES) || INCL_WINSOCK_API_PROTOTYPES)
#define DINO__WINSOCK2_H

WINSOCK_API_LINKAGE __typeof__(WSAStartup) WSAStartup __attribute__((deprecated(
    "WSAStartup() is Windows-only, and dino_socket_create() already calls it; use dino_net_init() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(WSACleanup) WSACleanup __attribute__((deprecated(
    "WSACleanup() is Windows-only; use dino_net_cleanup() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(socket) socket __attribute__((deprecated(
    "socket() returns a SOCKET, not an int, on Windows; use dino_socket_create() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(bind) bind __attribute__((deprecated(
    "bind() takes a SOCKET on Windows; use dino_socket_bind() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(listen) listen __attribute__((deprecated(
    "listen() takes a SOCKET on Windows; use dino_socket_listen() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(accept) accept __attribute__((deprecated(
    "accept() returns a SOCKET on Windows; use dino_socket_accept() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(connect) connect __attribute__((deprecated(
    "connect() reports errors through WSAGetLastError on Windows; use dino_socket_connect() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(send) send __attribute__((deprecated(
    "send() takes a char buffer and a SOCKET on Windows; use dino_socket_send() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(recv) recv __attribute__((deprecated(
    "recv() takes a char buffer and a SOCKET on Windows; use dino_socket_recv() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(setsockopt) setsockopt __attribute__((deprecated(
    "setsockopt() takes a const char * value on Windows; use dino_socket_setopt() from <dino/core/network.h>, which passes the option on as given (SO_RCVTIMEO and SO_LINGER still take different types per platform)")));
WINSOCK_API_LINKAGE __typeof__(shutdown) shutdown __attribute__((deprecated(
    "shutdown() uses different constants on Windows; use dino_socket_shutdown() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(closesocket) closesocket __attribute__((deprecated(
    "closesocket() is Windows-only; use dino_socket_close() from <dino/core/network.h>")));
WINSOCK_API_LINKAGE __typeof__(select) select __attribute__((deprecated(
    "select() ignores its first argument and takes SOCKET sets on Windows; use dino_select() from <dino/core/io.h>")));
#if _WIN32_WINNT >= 0x0600
WINSOCK_API_LINKAGE __typeof__(WSAPoll) WSAPoll __attribute__((deprecated(
    "WSAPoll() is Windows-only; use dino_poll() from <dino/core/io.h>")));
#endif

#endif
