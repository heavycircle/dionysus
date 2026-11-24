// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <winsock2.h>

#include <windows.h>
#endif

#include <string.h>
#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/event.h>
#include <dino/core/fs.h>
#include <dino/core/io.h>
#include <dino/core/mutex.h>
#include <dino/core/network.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>
#include <dino/core/thread.h>

#define PORT 18900

static dino_socket_t tcp(void)
{
    return dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
}

static dino_socket_t listener(int port)
{
    dino_socket_t s = tcp();
    if (s == DINO_INVALID_SOCKET)
        return s;
    dino_socket_set_reuseaddr(s, 1);
    if (dino_socket_bind(s, "127.0.0.1", port) != 0 || dino_socket_listen(s, 4) != 0)
    {
        dino_socket_close(s);
        return DINO_INVALID_SOCKET;
    }
    return s;
}

UTEST(error, every_code_has_a_message)
{
    for (int code = -1; code < 64; code++)
    {
        const char *message = dino_error_message(code);
        ASSERT_TRUE(message != NULL);
        EXPECT_GT(strlen(message), 0u);
    }
    EXPECT_STRNE(dino_error_message(DINO_EAGAIN), dino_error_message(DINO_ECONNREFUSED));
}

UTEST(error, invalid_arguments)
{
    EXPECT_EQ(-1, dino_socket_close(DINO_INVALID_SOCKET));
    EXPECT_EQ(DINO_EINVAL, dino_error());

    dino_socket_t s = tcp();
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(-1, dino_socket_bind(s, "not an address", PORT));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino_socket_send(s, NULL, 1));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    dino_socket_close(s);
}

// A call that succeeds leaves the code of the last failure alone.
UTEST(error, success_does_not_clear_it)
{
    EXPECT_EQ(-1, dino_socket_close(DINO_INVALID_SOCKET));
    dino_socket_t s = tcp();
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(0, dino_socket_close(s));
    EXPECT_EQ(DINO_EINVAL, dino_error());
}

static int fail_elsewhere(void *arg)
{
    int *seen = arg;
    *seen     = dino_error();
    dino_socket_recv(DINO_INVALID_SOCKET, NULL, 1);
    return dino_error();
}

UTEST(error, each_thread_has_its_own)
{
    dino_socket_t a = listener(PORT + 1);
    ASSERT_NE(DINO_INVALID_SOCKET, a);
    dino_socket_t b = tcp();
    ASSERT_NE(DINO_INVALID_SOCKET, b);
    ASSERT_EQ(-1, dino_socket_bind(b, "127.0.0.1", PORT + 1));
    ASSERT_EQ(DINO_EADDRINUSE, dino_error());

    dino_thread_t t;
    int           seen_at_start = -1, at_end = -1;
    ASSERT_EQ(0, dino_thread_create(&t, fail_elsewhere, &seen_at_start));
    ASSERT_EQ(0, dino_thread_join(t, &at_end));
    EXPECT_EQ(0, seen_at_start);
    EXPECT_EQ(DINO_EINVAL, at_end);
    EXPECT_EQ(DINO_EADDRINUSE, dino_error());

    dino_socket_close(b);
    dino_socket_close(a);
}

UTEST(error, nothing_to_read_or_accept_is_again)
{
    dino_socket_t server = listener(PORT + 2);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    ASSERT_EQ(0, dino_socket_set_nonblocking(server, 1));
    EXPECT_EQ(DINO_INVALID_SOCKET, dino_socket_accept(server));
    EXPECT_EQ(DINO_EAGAIN, dino_error());

    dino_socket_t client = tcp();
    ASSERT_EQ(0, dino_socket_connect(client, "127.0.0.1", PORT + 2));
    ASSERT_EQ(0, dino_socket_set_nonblocking(client, 1));
    char byte;
    EXPECT_EQ(-1, dino_socket_recv(client, &byte, 1));
    EXPECT_EQ(DINO_EAGAIN, dino_error());

    dino_socket_close(client);
    dino_socket_close(server);
}

UTEST(error, refused_connection)
{
    // Nothing listens here: bound, never listening, then closed.
    dino_socket_t client = tcp();
    ASSERT_NE(DINO_INVALID_SOCKET, client);
    EXPECT_EQ(-1, dino_socket_connect(client, "127.0.0.1", PORT + 3));
    EXPECT_EQ(DINO_ECONNREFUSED, dino_error());
    dino_socket_close(client);
}

static int wait_writable(dino_socket_t s)
{
    dino_pollfd_t pfd = { s, DINO_POLLOUT, 0 };
    return dino_poll(&pfd, 1, 5000);
}

// The portable way to finish a non-blocking connect: wait until the socket is
// writable, then ask it how the connect ended.
UTEST(error, nonblocking_connect_that_succeeds)
{
    dino_socket_t server = listener(PORT + 4);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    dino_socket_t client = tcp();
    ASSERT_EQ(0, dino_socket_set_nonblocking(client, 1));

    int rc = dino_socket_connect(client, "127.0.0.1", PORT + 4);
    if (rc != 0)
    {
        ASSERT_EQ(DINO_EINPROGRESS, dino_error());
        ASSERT_EQ(1, wait_writable(client));
    }
    EXPECT_EQ(0, dino_socket_error(client));

    dino_socket_close(client);
    dino_socket_close(server);
}

UTEST(error, nonblocking_connect_that_is_refused)
{
    dino_socket_t client = tcp();
    ASSERT_EQ(0, dino_socket_set_nonblocking(client, 1));

    int rc = dino_socket_connect(client, "127.0.0.1", PORT + 5);
    ASSERT_EQ(-1, rc);
    int code = dino_error();
    if (code == DINO_EINPROGRESS)
    {
        wait_writable(client);
        code = dino_socket_error(client);
    }
    EXPECT_EQ(DINO_ECONNREFUSED, code);

    dino_socket_close(client);
    EXPECT_EQ(DINO_EINVAL, dino_socket_error(DINO_INVALID_SOCKET));
}

UTEST(error, writing_to_a_closed_peer)
{
    dino_socket_t server = listener(PORT + 6);
    ASSERT_NE(DINO_INVALID_SOCKET, server);
    dino_socket_t client = tcp();
    ASSERT_EQ(0, dino_socket_connect(client, "127.0.0.1", PORT + 6));
    dino_socket_t peer = dino_socket_accept(server);
    ASSERT_NE(DINO_INVALID_SOCKET, peer);
    dino_socket_close(peer);

    // The first writes may still be accepted; one of them fails once the reset
    // has come back.
    int rc = 0;
    for (int i = 0; i < 100 && rc >= 0; i++)
    {
        rc = dino_socket_send(client, "x", 1);
        if (rc >= 0)
            dino_thread_yield();
    }
    ASSERT_EQ(-1, rc);
    int code = dino_error();
    EXPECT_TRUE(code == DINO_EPIPE || code == DINO_ECONNRESET);

    dino_socket_close(client);
    dino_socket_close(server);
}

// One failure per module that has a reason worth telling apart.

UTEST(error, files)
{
    uint64_t size;
    EXPECT_EQ(-1, dino_file_size("dino_test_no_such_file.bin", &size));
    EXPECT_EQ(DINO_ENOENT, dino_error());
    EXPECT_EQ(-1, dino_file_size(".", &size));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino_file_size(NULL, &size));
    EXPECT_EQ(DINO_EINVAL, dino_error());
}

UTEST(error, pipes)
{
    char byte;
    EXPECT_EQ(-1, dino_pipe_read(DINO_INVALID_PIPE, &byte, 1));
    EXPECT_EQ(DINO_EINVAL, dino_error());

    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    dino_pipe_close(r);
    EXPECT_EQ(-1, dino_pipe_write(w, "x", 1));
    EXPECT_EQ(DINO_EPIPE, dino_error());
    dino_pipe_close(w);
}

UTEST(error, processes)
{
    const char *const argv[] = { "dino-test-no-such-program", NULL };
    dino_process_t    process;
    int               rc = dino_process_spawn(&process, argv, NULL, NULL);
    if (rc == 0)
    {
        // Under valgrind (dino test --memcheck): glibc reports a failed exec
        // through CLONE_VFORK, which valgrind turns into a fork, so the failure
        // arrives as a child exiting 127, as a shell would report it.
        dino_process_status_t status = { -1, 0 };
        ASSERT_EQ(0, dino_process_wait(process, &status));
        EXPECT_EQ(127, status.code);
    }
    else
    {
        EXPECT_EQ(-1, rc);
        EXPECT_EQ(DINO_ENOENT, dino_error());
    }

    EXPECT_EQ(-1, dino_process_spawn(&process, NULL, NULL, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino_process_wait(DINO_INVALID_PROCESS, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
}

static int join_invalid(void *arg)
{
    (void)arg;
    return 0;
}

UTEST(error, threads_and_mutexes)
{
    EXPECT_EQ(-1, dino_thread_join(DINO_INVALID_THREAD, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    dino_thread_t t;
    EXPECT_EQ(-1, dino_thread_create(&t, NULL, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    ASSERT_EQ(0, dino_thread_create(&t, join_invalid, NULL));
    EXPECT_EQ(0, dino_thread_join(t, NULL));

    EXPECT_EQ(-1, dino_mutex_lock(DINO_INVALID_MUTEX));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    // Unlocking a mutex this thread doesn't hold.
    dino_mutex_t m = dino_mutex_create();
    ASSERT_NE(DINO_INVALID_MUTEX, m);
    EXPECT_EQ(-1, dino_mutex_unlock(m));
    EXPECT_EQ(DINO_EACCES, dino_error());
    dino_mutex_destroy(m);
}

UTEST(error, event_loop)
{
    dino_event_loop_t loop = dino_event_loop_create();
    ASSERT_NE(DINO_INVALID_EVENT_LOOP, loop);
    dino_socket_t s = listener(PORT + 7);
    ASSERT_NE(DINO_INVALID_SOCKET, s);

    EXPECT_EQ(-1, dino_event_mod_socket(loop, s, DINO_EVENT_READ, NULL));
    EXPECT_EQ(DINO_ENOENT, dino_error());
    EXPECT_EQ(-1, dino_event_del_socket(loop, s));
    EXPECT_EQ(DINO_ENOENT, dino_error());
    ASSERT_EQ(0, dino_event_add_socket(loop, s, DINO_EVENT_READ, NULL));
    EXPECT_EQ(-1, dino_event_add_socket(loop, s, DINO_EVENT_READ, NULL));
    EXPECT_EQ(DINO_EEXIST, dino_error());
    EXPECT_EQ(-1, dino_event_add_socket(loop, DINO_INVALID_SOCKET, DINO_EVENT_READ, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());

    EXPECT_EQ(DINO_INVALID_EVENT_TIMER, dino_event_add_timer(DINO_INVALID_EVENT_LOOP, 1, 0, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    dino_event_t out[1];
    EXPECT_EQ(-1, dino_event_wait(loop, out, 0, 0));
    EXPECT_EQ(DINO_EINVAL, dino_error());

    EXPECT_EQ(0, dino_event_del_socket(loop, s));
    dino_socket_close(s);
    dino_event_loop_destroy(loop);
}

UTEST_MAIN()
