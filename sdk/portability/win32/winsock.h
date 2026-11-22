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
    "WSAStartup() is Windows-only, and dino_socket_create() already calls it; use dino_net_init() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(WSACleanup) WSACleanup __attribute__((deprecated(
    "WSACleanup() is Windows-only; use dino_net_cleanup() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(socket) socket __attribute__((deprecated(
    "socket() returns a SOCKET, not an int, on Windows; use dino_socket_create() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(bind) bind __attribute__((deprecated(
    "bind() takes a SOCKET on Windows; use dino_socket_bind() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(listen) listen __attribute__((deprecated(
    "listen() takes a SOCKET on Windows; use dino_socket_listen() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(accept) accept __attribute__((deprecated(
    "accept() returns a SOCKET on Windows; use dino_socket_accept() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(connect) connect __attribute__((deprecated(
    "connect() reports errors through WSAGetLastError on Windows; use dino_socket_connect() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(send) send __attribute__((deprecated(
    "send() takes a char buffer and a SOCKET on Windows; use dino_socket_send() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(recv) recv __attribute__((deprecated(
    "recv() takes a char buffer and a SOCKET on Windows; use dino_socket_recv() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(setsockopt) setsockopt __attribute__((deprecated(
    "setsockopt() takes a const char * value on Windows; use dino_socket_setopt() from <dino/core/network.h>, which passes the option on as given (SO_RCVTIMEO and SO_LINGER still take different types per platform)")));
DECLSPEC_IMPORT __typeof__(shutdown) shutdown __attribute__((deprecated(
    "shutdown() uses different constants on Windows; use dino_socket_shutdown() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(closesocket) closesocket __attribute__((deprecated(
    "closesocket() is Windows-only; use dino_socket_close() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(select) select __attribute__((deprecated(
    "select() ignores its first argument and takes SOCKET sets on Windows; use dino_select() from <dino/core/io.h>")));
DECLSPEC_IMPORT __typeof__(ioctlsocket) ioctlsocket __attribute__((deprecated(
    "ioctlsocket() is Windows-only; use dino_socket_set_nonblocking() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(sendto) sendto __attribute__((deprecated(
    "sendto() needs WSAStartup on Windows; use dino_socket_sendto() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(recvfrom) recvfrom __attribute__((deprecated(
    "recvfrom() fails on a datagram larger than the buffer on Windows; use dino_socket_recvfrom() from <dino/core/network.h>")));
DECLSPEC_IMPORT __typeof__(WSAGetLastError) WSAGetLastError __attribute__((deprecated(
    "WSAGetLastError() is Windows-only; use dino_error() from <dino/core/error.h>")));

#endif
