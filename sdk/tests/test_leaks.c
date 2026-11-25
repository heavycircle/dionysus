// Descriptors and handles, which the leak check (heap only) and valgrind
// (Linux only) don't see: each module's create/use/destroy cycle, failure
// paths included, run many times must leave the count where it was.
#ifdef _WIN32
#include <winsock2.h>

#include <windows.h>
#else
#include <dirent.h>
#endif

#include <utest.h>

#include <dino/core/event.h>
#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>

#include "../src/core/thread_internal.h"
#include "net_helpers.h"

#define CYCLES 20

#ifdef _WIN32
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

static int open_handles(void)
{
#ifdef _WIN32
    DWORD count = 0;
    return GetProcessHandleCount(GetCurrentProcess(), &count) ? (int)count : -1;
#else
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return -1;
    int count = 0;
    while (readdir(dir))
        count++;
    closedir(dir);
    return count; // the same "." , ".." and the directory's own descriptor each time
#endif
}

// Runs cycle once to let first-use setup happen (Winsock, thread pools), then
// CYCLES times; the count afterwards must not have grown. Windows' own thread
// pool can hold a few handles more or less, so a little slack there.
#define EXPECT_NO_LEAK(cycle)                                                                      \
    do                                                                                             \
    {                                                                                              \
        ASSERT_EQ(0, cycle());                                                                     \
        int before = open_handles();                                                               \
        ASSERT_GE(before, 0);                                                                      \
        /* wine's GetProcessHandleCount says 0: there's nothing to compare */                      \
        if (before == 0)                                                                           \
            break;                                                                                 \
        for (int i = 0; i < CYCLES; i++)                                                           \
            ASSERT_EQ(0, cycle());                                                                 \
        EXPECT_LE(open_handles(), before + SLACK);                                                 \
    } while (0)

#ifdef _WIN32
#define SLACK 4
#else
#define SLACK 0
#endif

static int pipe_cycle(void)
{
    dino_pipe_t r, w;
    if (dino_pipe_create(&r, &w) != 0)
        return -1;
    int  ok = dino_pipe_write(w, "x", 1) == 1;
    char c;
    ok = ok && dino_pipe_read(r, &c, 1) == 1;
    dino_pipe_close(r);
    dino_pipe_close(w);
    return ok ? 0 : -1;
}

UTEST(leaks, pipes)
{
    EXPECT_NO_LEAK(pipe_cycle);
}

static int socket_cycle(void)
{
    dino_socket_t server = bound_socket(AF_INET, SOCK_STREAM, "127.0.0.1", 1);
    dino_socket_t client = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (server == DINO_INVALID_SOCKET || client == DINO_INVALID_SOCKET)
        return -1;
    int           ok   = connect_to(client, "127.0.0.1", local_port(server)) == 0;
    dino_socket_t peer = dino_socket_accept(server, NULL, NULL);
    ok                 = ok && peer != DINO_INVALID_SOCKET;
    if (peer != DINO_INVALID_SOCKET)
        dino_socket_close(peer);
    dino_socket_close(client);
    dino_socket_close(server);

    // And a refused connect, a failure path.
    dino_socket_t holder;
    int           port = refusing_port(&holder);
    dino_socket_t s    = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    ok                 = ok && port > 0 && connect_to(s, "127.0.0.1", port) == -1;
    dino_socket_close(s);
    dino_socket_close(holder);
    return ok ? 0 : -1;
}

UTEST(leaks, sockets)
{
    EXPECT_NO_LEAK(socket_cycle);
}

// A loop with one of everything in it, some of them still registered when
// it's destroyed.
static int event_cycle(void)
{
    dino_event_loop_t loop = dino_event_loop_create();
    if (loop == DINO_INVALID_EVENT_LOOP)
        return -1;
    dino_pipe_t   r, w;
    dino_socket_t s      = dino_socket_create(AF_INET, SOCK_DGRAM, 0);
    int           ok     = dino_pipe_create(&r, &w) == 0 && s != DINO_INVALID_SOCKET;
    ok                   = ok && dino_event_add_pipe(loop, r, DINO_EVENT_READ, NULL) == 0;
    ok                   = ok && dino_event_add_socket(loop, s, DINO_EVENT_WRITE, NULL) == 0;
    dino_event_timer_t t = dino_event_add_timer(loop, 0, 0, NULL);
    ok                   = ok && t != DINO_INVALID_EVENT_TIMER && dino_event_wake(loop) == 0;
    dino_event_t out[8];
    ok = ok && dino_event_wait(loop, out, 8, 1000) >= 1;
    dino_event_del_socket(loop, s);
    dino_event_loop_destroy(loop);
    dino_socket_close(s);
    dino_pipe_close(r);
    dino_pipe_close(w);
    return ok ? 0 : -1;
}

UTEST(leaks, event_loops)
{
    EXPECT_NO_LEAK(event_cycle);
}

static int spawn_cycle(void)
{
    const char *const         argv[] = SHELL("echo hi");
    dino_process_spawn_opts_t opts   = { 0 };
    opts.stdin_mode                  = DINO_STDIO_PIPE;
    opts.stdout_mode                 = DINO_STDIO_PIPE;
    opts.stderr_mode                 = DINO_STDIO_PIPE;
    dino_process_t       p;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&p, argv, &opts, &pipes) != 0)
        return -1;
    dino_event_loop_t loop = dino_event_loop_create();
    int               ok   = dino_event_add_process(loop, p, NULL) == 0;
    dino_event_t      out[2];
    ok = ok && dino_event_wait(loop, out, 2, 10000) == 1;
    dino_event_del_process(loop, p);
    dino_event_loop_destroy(loop);
    dino_pipe_close(pipes.in);
    char buf[16];
    while (dino_pipe_read(pipes.out, buf, sizeof(buf)) > 0)
        ;
    dino_pipe_close(pipes.out);
    dino_pipe_close(pipes.err);
    ok = ok && dino_process_wait(p, NULL) == 0;

    // The failure path, after the pipes are made.
    const char *const missing[] = { "dino-test-no-such-program", NULL };
    if (dino_process_spawn(&p, missing, &opts, &pipes) == 0)
    {
        // valgrind's glibc reports the failure as exit 127 instead.
        dino_pipe_close(pipes.in);
        dino_pipe_close(pipes.out);
        dino_pipe_close(pipes.err);
        dino_process_wait(p, NULL);
    }
    return ok ? 0 : -1;
}

UTEST(leaks, processes)
{
    EXPECT_NO_LEAK(spawn_cycle);
}

static int run_cycle(void)
{
    const char *const       argv[] = SHELL("echo hi");
    dino_process_run_opts_t opts   = { 0 };
    opts.input                     = "x";
    opts.input_len                 = 1;
    dino_process_result_t r;
    int                   ok = dino_process_run(argv, &opts, &r) == 0 && r.status.code == 0;
    dino_process_result_free(&r);
    return ok ? 0 : -1;
}

UTEST(leaks, process_run)
{
    EXPECT_NO_LEAK(run_cycle);
}

static int nothing(void *arg)
{
    (void)arg;
    return 0;
}

static int thread_cycle(void)
{
    dino__thread_t t;
    if (dino__thread_create(&t, nothing, NULL) != 0)
        return -1;
    return dino__thread_join(t, NULL);
}

UTEST(leaks, threads)
{
    EXPECT_NO_LEAK(thread_cycle);
}

UTEST_MAIN()
