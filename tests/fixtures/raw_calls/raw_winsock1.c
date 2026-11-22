// clang-format off: clang -verify reads each call's expectation from that call's own line.
// Only <windows.h>, without WIN32_LEAN_AND_MEAN: it brings in Winsock 1, whose
// calls must warn as <winsock2.h>'s do.
#include <windows.h>

void raw_winsock1(void);
void raw_winsock1(void)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0); // expected-warning {{use dino_socket_create()}}
    closesocket(s);                             // expected-warning {{use dino_socket_close()}}
    (void)WSAGetLastError();                    // expected-warning {{use dino_error()}}
}
