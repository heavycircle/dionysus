#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <winsock.h>

#include "dino_portability.h"

// Winsock 1, which <windows.h> includes unless WIN32_LEAN_AND_MEAN is defined:
// the same calls as <winsock2.h>, so code that only includes <windows.h> hears
// about them too. DECLSPEC_IMPORT keeps the redeclarations dllimport.
#if defined(DINO__WARNINGS) && !defined(DINO__WINSOCK_H) && defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#define DINO__WINSOCK_H

DECLSPEC_IMPORT __typeof__(WSAStartup) WSAStartup __attribute__((deprecated(
    "WSAStartup() is Windows-only, and the SDK starts Winsock itself; to check it started, use dino_net_init() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(WSACleanup) WSACleanup __attribute__((deprecated(
    "WSACleanup() is Windows-only, and it breaks every open socket; nothing needs calling, as Windows releases Winsock at exit")));
DECLSPEC_IMPORT __typeof__(socket) socket __attribute__((deprecated(
    "socket() returns a SOCKET, not an int, on Windows; use dino_socket_create() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(accept) accept __attribute__((deprecated(
    "accept() returns a SOCKET on Windows; use dino_socket_accept() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(connect) connect __attribute__((deprecated(
    "connect() reports errors through WSAGetLastError on Windows; use dino_socket_connect() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(send) send __attribute__((deprecated(
    "send() takes a char buffer and a SOCKET on Windows; use dino_socket_send() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(closesocket) closesocket __attribute__((deprecated(
    "closesocket() is Windows-only; use dino_socket_close() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(ioctlsocket) ioctlsocket __attribute__((deprecated(
    "ioctlsocket() is Windows-only; use dino_socket_set_nonblocking() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(recvfrom) recvfrom __attribute__((deprecated(
    "recvfrom() fails on a datagram larger than the buffer on Windows; use dino_socket_recvfrom() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(WSAGetLastError) WSAGetLastError __attribute__((deprecated(
    "WSAGetLastError() is Windows-only; the dino_socket_* calls report errors through dino_error() from <dino/core/error.h>")));

#endif
