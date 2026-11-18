// clang-format off: clang -verify reads each call's expectation from that call's own line.
#include <winsock2.h>
#include <windows.h>
#include <process.h>

static DWORD WINAPI thread_main(LPVOID arg)
{
    (void)arg;
    return 0;
}

static unsigned __stdcall thread_main_ex(void *arg)
{
    (void)arg;
    return 0;
}

static void thread_main_crt(void *arg)
{
    (void)arg;
}

void raw_win32(void);
void raw_win32(void)
{
    WSADATA             wsa;
    HANDLE              r, w;
    CRITICAL_SECTION    cs;
    LARGE_INTEGER       now;
    STARTUPINFOA        sia  = { 0 };
    STARTUPINFOW        siw  = { 0 };
    PROCESS_INFORMATION pi;
    struct sockaddr     addr = { 0 };
    int                 len = sizeof(addr);
    int                 opt = 1;
    fd_set              set;
    char                buf[1];

    sia.cb = sizeof(sia);
    siw.cb = sizeof(siw);

    Sleep(1);                                       // expected-warning {{use dino_sleep_ms()}}
    CreateThread(NULL, 0, thread_main, NULL, 0, NULL); // expected-warning {{use dino_thread_create()}}
    _beginthreadex(NULL, 0, thread_main_ex, NULL, 0, NULL); // expected-warning {{_beginthreadex() is Windows-only}}
    _beginthread(thread_main_crt, 0, NULL);         // expected-warning {{_beginthread() is Windows-only}}
    CreatePipe(&r, &w, NULL, 0);                    // expected-warning {{use dino_pipe_create()}}
    CreateProcessA(NULL, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &sia, &pi); // expected-warning {{use dino_process_spawn()}}
    CreateProcessW(NULL, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &siw, &pi); // expected-warning {{use dino_process_spawn()}}
    InitializeCriticalSection(&cs);                 // expected-warning {{use dino_mutex_create()}}
    InitializeCriticalSectionAndSpinCount(&cs, 0);  // expected-warning {{use dino_mutex_create()}}
    QueryPerformanceCounter(&now);                  // expected-warning {{use dino_time_monotonic_ns()}}

    WSAStartup(MAKEWORD(2, 2), &wsa);               // expected-warning {{WSAStartup() is Windows-only}}
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);     // expected-warning {{use dino_socket_create()}}
    bind(s, &addr, len);                            // expected-warning {{use dino_socket_bind()}}
    listen(s, 1);                                   // expected-warning {{use dino_socket_listen()}}
    accept(s, &addr, &len);                         // expected-warning {{use dino_socket_accept()}}
    connect(s, &addr, len);                         // expected-warning {{use dino_socket_connect()}}
    send(s, "x", 1, 0);                             // expected-warning {{use dino_socket_send()}}
    recv(s, buf, 1, 0);                             // expected-warning {{use dino_socket_recv()}}
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt)); // expected-warning {{use dino_socket_setopt()}}
    shutdown(s, SD_BOTH);                           // expected-warning {{use dino_socket_shutdown()}}
    FD_ZERO(&set);
    select(0, &set, NULL, NULL, NULL);              // expected-warning {{use dino_select()}}
#if _WIN32_WINNT >= 0x0600
    WSAPOLLFD pfd = { s, POLLIN, 0 };
    WSAPoll(&pfd, 1, 0);                            // expected-warning {{use dino_poll()}}
#endif
    closesocket(s);                                 // expected-warning {{use dino_socket_close()}}
    WSACleanup();                                   // expected-warning {{WSACleanup() is Windows-only}}

    // No dino replacement, or too general to flag.
    CloseHandle(r);
    WaitForSingleObject(w, 0);
}
