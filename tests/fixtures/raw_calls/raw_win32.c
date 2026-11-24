// clang-format off: clang -verify reads each call's expectation from that call's own line.
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdlib.h>

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

static BOOL CALLBACK init_once_cb(PINIT_ONCE once, PVOID param, PVOID *context)
{
    (void)once;
    (void)param;
    (void)context;
    return TRUE;
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
    CreateThread(NULL, 0, thread_main, NULL, 0, NULL); // expected-warning {{use pthread_create()}}
    _beginthreadex(NULL, 0, thread_main_ex, NULL, 0, NULL); // expected-warning {{_beginthreadex() is Windows-only}}
    _beginthread(thread_main_crt, 0, NULL);         // expected-warning {{_beginthread() is Windows-only}}
    CreatePipe(&r, &w, NULL, 0);                    // expected-warning {{use dino_pipe_create()}}
    CreateProcessA(NULL, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &sia, &pi); // expected-warning {{use dino_process_spawn()}}
    CreateProcessW(NULL, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &siw, &pi); // expected-warning {{use dino_process_spawn()}}
    InitializeCriticalSection(&cs);                 // expected-warning {{use pthread_mutex_init()}}
    InitializeCriticalSectionAndSpinCount(&cs, 0);  // expected-warning {{use pthread_mutex_init()}}
    QueryPerformanceCounter(&now);                  // expected-warning {{use clock_gettime(CLOCK_MONOTONIC)}}
    TerminateProcess(pi.hProcess, 1);               // expected-warning {{use dino_process_kill()}}
    (void)GetCurrentProcessId();                    // expected-warning {{use getpid() from <unistd.h>}}
    (void)GetCurrentThreadId();                     // expected-warning {{use dino_thread_current_id()}}
    SwitchToThread();                               // expected-warning {{use sched_yield()}}
    SleepEx(0, FALSE);                              // expected-warning {{use dino_sleep_ms()}}
    GetEnvironmentVariableA("X", buf, 1);           // expected-warning {{use getenv() from <stdlib.h>}}
    GetEnvironmentVariableW(L"X", NULL, 0);         // expected-warning {{use getenv() from <stdlib.h>}}
    SetEnvironmentVariableA("X", "1");              // expected-warning {{use setenv() or unsetenv()}}
    SetEnvironmentVariableW(L"X", NULL);            // expected-warning {{use setenv() or unsetenv()}}
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);                   // expected-warning {{use timespec_get(&ts, TIME_UTC)}}
    GetFileSizeEx(r, &now);                         // expected-warning {{use dino_file_size()}}
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0); // expected-warning {{use dino_event_loop_create()}}
    DWORD        bytes;
    ULONG_PTR    key;
    LPOVERLAPPED ov;
    GetQueuedCompletionStatus(port, &bytes, &key, &ov, 0); // expected-warning {{use dino_event_wait()}}
#if _WIN32_WINNT >= 0x0600
    (void)GetTickCount64();                         // expected-warning {{use clock_gettime(CLOCK_MONOTONIC)}}
    OVERLAPPED_ENTRY entry;
    ULONG            got;
    GetQueuedCompletionStatusEx(port, &entry, 1, &got, 0, FALSE); // expected-warning {{use dino_event_wait()}}
    INIT_ONCE io = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&io, init_once_cb, NULL, NULL); // expected-warning {{use pthread_once()}}
    CONDITION_VARIABLE cv;
    InitializeConditionVariable(&cv);               // expected-warning {{use pthread_cond_init()}}
    SleepConditionVariableCS(&cv, &cs, 0);          // expected-warning {{use pthread_cond_wait()}}
#endif
    (void)_getpid();                                // expected-warning {{use getpid() from <unistd.h>}}
    _cwait(NULL, 0, 0);                             // expected-warning {{use dino_process_wait()}}
    _spawnl(_P_NOWAIT, "x", "x", NULL);             // expected-warning {{_spawnl() is Windows-only}}
    _spawnv(_P_NOWAIT, "x", NULL);                  // expected-warning {{_spawnv() is Windows-only}}
    _execl("x", "x", NULL);                         // expected-warning {{_execl() is Windows-only}}
    _execv("x", NULL);                              // expected-warning {{_execv() is Windows-only}}
    _putenv("X=1");                                 // expected-warning {{use setenv() or unsetenv()}}

    WSAStartup(MAKEWORD(2, 2), &wsa);               // expected-warning {{WSAStartup() is Windows-only}}
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);     // expected-warning {{use dino_socket_create()}}
    bind(s, &addr, len);
    listen(s, 1);
    accept(s, &addr, &len);                         // expected-warning {{use dino_socket_accept()}}
    connect(s, &addr, len);                         // expected-warning {{use dino_socket_connect()}}
    send(s, "x", 1, 0);                             // expected-warning {{use dino_socket_send()}}
    recv(s, buf, 1, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
    shutdown(s, SD_BOTH);
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);                 // expected-warning {{use dino_socket_set_nonblocking()}}
    sendto(s, "x", 1, 0, &addr, len);
    recvfrom(s, buf, 1, 0, &addr, &len);            // expected-warning {{use dino_socket_recvfrom()}}
    (void)WSAGetLastError();                        // expected-warning {{report errors through dino_error()}}
    WSASocketA(AF_INET, SOCK_STREAM, 0, NULL, 0, 0); // expected-warning {{WSASocketA() is Windows-only}}
    WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, 0); // expected-warning {{WSASocketW() is Windows-only}}
    FD_ZERO(&set);
    select(0, &set, NULL, NULL, NULL);
#if _WIN32_WINNT >= 0x0600
    WSAPOLLFD pfd = { s, POLLIN, 0 };
    WSAPoll(&pfd, 1, 0);                            // expected-warning {{use poll() from <poll.h>}}
#endif
    closesocket(s);                                 // expected-warning {{use dino_socket_close()}}
    WSACleanup();                                   // expected-warning {{WSACleanup() is Windows-only}}

    // No dino replacement, or too general to flag.
    CloseHandle(r);
    WaitForSingleObject(w, 0);
}
