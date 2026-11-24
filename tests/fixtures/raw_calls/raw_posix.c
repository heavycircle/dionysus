// clang-format off: clang -verify reads each call's expectation from that call's own line.
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

static int thread_main(void *arg)
{
    (void)arg;
    return 0;
}

static void init_once(void) {}

void raw_posix(char **argv, char **envp);
void raw_posix(char **argv, char **envp)
{
    int             fds[2];
    int             opt = 1;
    pid_t           pid;
    struct pollfd   pfd = { 0, 0, 0 };
    struct sockaddr addr = { 0 };
    socklen_t       len = sizeof(addr);
    fd_set          set;

    pipe(fds); // expected-warning {{pipe() is POSIX-only}}
#ifdef _GNU_SOURCE
    pipe2(fds, 0); // expected-warning {{pipe2() is Linux-only}}
#endif
    pid = fork();                          // expected-warning {{fork() doesn't exist on Windows}}
    execv("/bin/sh", argv);                // expected-warning {{execv() is POSIX-only}}
    execvp("sh", argv);                    // expected-warning {{execvp() is POSIX-only}}
    execve("/bin/sh", argv, envp);         // expected-warning {{execve() is POSIX-only}}
    execl("/bin/sh", "sh", (char *)NULL);  // expected-warning {{execl() is POSIX-only}}
    execlp("sh", "sh", (char *)NULL);      // expected-warning {{execlp() is POSIX-only}}
    execle("/bin/sh", "sh", (char *)NULL, envp); // expected-warning {{execle() is POSIX-only}}
    posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, envp); // expected-warning {{posix_spawn() is POSIX-only}}
    posix_spawnp(&pid, "sh", NULL, NULL, argv, envp);     // expected-warning {{posix_spawnp() is POSIX-only}}
    waitpid(pid, NULL, 0);                 // expected-warning {{waitpid() is POSIX-only}}
    wait(NULL);                            // expected-warning {{use dino_process_wait()}}
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
    siginfo_t info;
    waitid(P_PID, (id_t)pid, &info, WEXITED); // expected-warning {{waitid() is POSIX-only}}
    kill(pid, SIGTERM);                    // expected-warning {{use dino_process_kill()}}
    setenv("X", "1", 1);
    unsetenv("X");
#endif
#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
    wait4(pid, NULL, 0, NULL);             // expected-warning {{wait4() is POSIX-only}}
#endif
#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
    static char env_entry[] = "X=1";
    putenv(env_entry);                     // expected-warning {{putenv() keeps the caller's string}}
#endif
#ifdef _GNU_SOURCE
    clearenv();                            // expected-warning {{clearenv() is Linux-only}}
    (void)gettid();                        // expected-warning {{use dino_thread_current_id()}}
#endif
    (void)getpid();

    int s = socket(AF_INET, SOCK_STREAM, 0); // expected-warning {{use dino_socket_create()}}
    bind(s, &addr, len);
    listen(s, 1);
    accept(s, &addr, &len);                // expected-warning {{use dino_socket_accept()}}
    connect(s, &addr, len);                // expected-warning {{use dino_socket_connect()}}
    send(s, "x", 1, 0);                    // expected-warning {{use dino_socket_send()}}
    recv(s, fds, 1, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    shutdown(s, SHUT_RDWR);
    sendto(s, "x", 1, 0, &addr, len);
    recvfrom(s, fds, 1, 0, &addr, &len);   // expected-warning {{use dino_socket_recvfrom()}}
#ifdef _GNU_SOURCE
    accept4(s, &addr, &len, 0);            // expected-warning {{accept4() is Linux-only}}
#endif

    poll(&pfd, 1, 0);                      // expected-warning {{use dino_poll()}}
    FD_ZERO(&set);
    select(1, &set, NULL, NULL, NULL);     // expected-warning {{use dino_select()}}
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
    pselect(1, &set, NULL, NULL, NULL, NULL); // expected-warning {{pselect() is POSIX-only}}
#endif
#ifdef _GNU_SOURCE
    ppoll(&pfd, 1, NULL, NULL);            // expected-warning {{ppoll() is Linux-only}}
#endif

    int ep = epoll_create(1);              // expected-warning {{use dino_event_loop_create()}}
    epoll_create1(0);                      // expected-warning {{use dino_event_loop_create()}}
    struct epoll_event ev = { 0 };
    epoll_ctl(ep, EPOLL_CTL_ADD, s, &ev);  // expected-warning {{epoll_ctl() is Linux-only}}
    epoll_wait(ep, &ev, 1, 0);             // expected-warning {{use dino_event_wait()}}
    epoll_pwait(ep, &ev, 1, 0, NULL);      // expected-warning {{use dino_event_wait()}}

    thrd_t    thr;
    mtx_t     mtx;
    cnd_t     cnd;
    once_flag flag = ONCE_FLAG_INIT;
    thrd_create(&thr, thread_main, NULL);  // expected-warning {{use pthread_create()}}
    thrd_join(thr, NULL);                  // expected-warning {{use pthread_join()}}
    thrd_detach(thr);                      // expected-warning {{use pthread_detach()}}
    thrd_yield();                          // expected-warning {{use sched_yield()}}
    mtx_init(&mtx, mtx_plain);             // expected-warning {{use pthread_mutex_init()}}
    mtx_lock(&mtx);                        // expected-warning {{use pthread_mutex_lock()}}
    mtx_trylock(&mtx);                     // expected-warning {{use pthread_mutex_trylock()}}
    mtx_unlock(&mtx);                      // expected-warning {{use pthread_mutex_unlock()}}
    cnd_init(&cnd);                        // expected-warning {{use pthread_cond_init()}}
    cnd_wait(&cnd, &mtx);                  // expected-warning {{use pthread_cond_wait()}}
    struct timespec until = { 0, 0 };
    cnd_timedwait(&cnd, &mtx, &until);     // expected-warning {{use dino_cond_timedwait()}}
    cnd_signal(&cnd);                      // expected-warning {{use pthread_cond_signal()}}
    cnd_broadcast(&cnd);                   // expected-warning {{use pthread_cond_broadcast()}}
    cnd_destroy(&cnd);                     // expected-warning {{use pthread_cond_destroy()}}
    mtx_destroy(&mtx);                     // expected-warning {{use pthread_mutex_destroy()}}
    call_once(&flag, init_once);           // expected-warning {{use pthread_once()}}

#ifdef CLOCK_REALTIME
    struct timespec ts = { 0, 0 };
    nanosleep(&ts, NULL);                  // expected-warning {{nanosleep() rounds down}}
    clock_gettime(CLOCK_MONOTONIC, &ts);
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL); // expected-warning {{clock_nanosleep() rounds down}}
#endif

    // Still quiet: right on files, or no dino replacement.
    close(write(fds[1], "x", 1));
    (void)getenv("X");
}
