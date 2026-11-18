// clang-format off: clang -verify reads each call's expectation from that call's own line.
#include <poll.h>
#include <spawn.h>
#include <stddef.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

    int s = socket(AF_INET, SOCK_STREAM, 0); // expected-warning {{use dino_socket_create()}}
    bind(s, &addr, len);                   // expected-warning {{use dino_socket_bind()}}
    listen(s, 1);                          // expected-warning {{use dino_socket_listen()}}
    accept(s, &addr, &len);                // expected-warning {{use dino_socket_accept()}}
    connect(s, &addr, len);                // expected-warning {{use dino_socket_connect()}}
    send(s, "x", 1, 0);                    // expected-warning {{use dino_socket_send()}}
    recv(s, fds, 1, 0);                    // expected-warning {{use dino_socket_recv()}}
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // expected-warning {{use dino_socket_setopt()}}
    shutdown(s, SHUT_RDWR);                // expected-warning {{use dino_socket_shutdown()}}

    poll(&pfd, 1, 0);                      // expected-warning {{use dino_poll()}}
    FD_ZERO(&set);
    select(1, &set, NULL, NULL, NULL);     // expected-warning {{use dino_select()}}

#ifdef CLOCK_REALTIME
    struct timespec ts = { 0, 0 };
    nanosleep(&ts, NULL);                  // expected-warning {{use dino_sleep_us()}}
    clock_gettime(CLOCK_MONOTONIC, &ts);   // expected-warning {{use dino_time_monotonic_ns()}}
#endif

    // Still quiet: right on files, or no dino replacement.
    close(write(fds[1], "x", 1));
    (void)getpid();
}
