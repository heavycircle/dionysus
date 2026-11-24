// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#endif

#include <stdint.h>
#include <string.h>
#include <utest.h>

#include <dino/core/pipe.h>
#include <dino/core/time.h>

#include "../src/core/thread_internal.h"

UTEST(pipe, round_trip)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    ASSERT_NE(DINO_INVALID_PIPE, r);
    ASSERT_NE(DINO_INVALID_PIPE, w);

    EXPECT_EQ(5, dino_pipe_write(w, "hello", 5));
    char buf[8] = { 0 };
    EXPECT_EQ(5, dino_pipe_read(r, buf, sizeof(buf)));
    EXPECT_STREQ("hello", buf);

    EXPECT_EQ(0, dino_pipe_close(r));
    EXPECT_EQ(0, dino_pipe_close(w));
}

UTEST(pipe, read_returns_zero_after_the_write_end_closes)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(3, dino_pipe_write(w, "abc", 3));
    EXPECT_EQ(0, dino_pipe_close(w));

    char buf[8];
    EXPECT_EQ(3, dino_pipe_read(r, buf, sizeof(buf)));
    EXPECT_EQ(0, dino_pipe_wait(r, 0));
    EXPECT_EQ(0, dino_pipe_read(r, buf, sizeof(buf)));
    EXPECT_EQ(0, dino_pipe_close(r));
}

UTEST(pipe, wait_times_out_on_an_empty_pipe_and_sees_data)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));

    EXPECT_EQ(DINO_TIMEOUT, dino_pipe_wait(r, 0));
    uint64_t start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_pipe_wait(r, 50));
    EXPECT_GE((dino_time_monotonic_ns() - start) / 1000000, 50u);

    // Never early, not even by the part of a scheduler tick that Windows'
    // own waits can be short. Short waits make that easy to see.
    for (int i = 0; i < 40; i++)
    {
        start = dino_time_monotonic_ns();
        ASSERT_EQ(DINO_TIMEOUT, dino_pipe_wait(r, 3));
        ASSERT_GE(dino_time_monotonic_ns() - start, 3u * 1000000u);
    }

    EXPECT_EQ(1, dino_pipe_write(w, "x", 1));
    EXPECT_EQ(0, dino_pipe_wait(r, 0));
    EXPECT_EQ(0, dino_pipe_wait(r, -1));

    dino_pipe_close(r);
    dino_pipe_close(w);
}

static int write_later(void *arg)
{
    dino_sleep_ms(30);
    return dino_pipe_write(*(dino_pipe_t *)arg, "late", 4) == 4 ? 0 : -1;
}

UTEST(pipe, wait_wakes_when_another_thread_writes)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, write_later, &w));

    EXPECT_EQ(0, dino_pipe_wait(r, 10000));
    char buf[8] = { 0 };
    EXPECT_EQ(4, dino_pipe_read(r, buf, sizeof(buf)));
    EXPECT_STREQ("late", buf);

    int result = -1;
    EXPECT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(0, result);
    dino_pipe_close(r);
    dino_pipe_close(w);
}

// Far more than the 64 KB a pipe holds, so the writer blocks until the reader
// catches up; partial writes are continued.
#define BIG (1 << 20)

static unsigned char byte_at(int i)
{
    return (unsigned char)(i * 31 + 7);
}

static int write_big(void *arg)
{
    dino_pipe_t   w = *(dino_pipe_t *)arg;
    unsigned char chunk[4096];
    for (int sent = 0; sent < BIG;)
    {
        int n = BIG - sent < (int)sizeof(chunk) ? BIG - sent : (int)sizeof(chunk);
        for (int i = 0; i < n; i++)
            chunk[i] = byte_at(sent + i);
        int done = 0;
        while (done < n)
        {
            int rc = dino_pipe_write(w, chunk + done, n - done);
            if (rc <= 0)
                return -1;
            done += rc;
        }
        sent += n;
    }
    return dino_pipe_close(w);
}

UTEST(pipe, large_transfer_between_threads)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    dino__thread_t t;
    ASSERT_EQ(0, dino__thread_create(&t, write_big, &w));

    int           received = 0, mismatches = 0, n;
    unsigned char buf[8192];
    while ((n = dino_pipe_read(r, buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
            mismatches += buf[i] != byte_at(received + i);
        received += n;
    }
    EXPECT_EQ(0, n);
    EXPECT_EQ(BIG, received);
    EXPECT_EQ(0, mismatches);

    int result = -1;
    EXPECT_EQ(0, dino__thread_join(t, &result));
    EXPECT_EQ(0, result);
    dino_pipe_close(r);
}

// Internal to the SDK (pipe_internal.h): the HANDLE or file descriptor.
intptr_t dino__pipe_to_os(dino_pipe_t pipe);

static int inheritable(dino_pipe_t p)
{
#ifdef _WIN32
    DWORD flags = 0;
    if (!GetHandleInformation((HANDLE)dino__pipe_to_os(p), &flags))
        return -1;
    return (flags & HANDLE_FLAG_INHERIT) != 0;
#else
    int flags = fcntl((int)dino__pipe_to_os(p), F_GETFD);
    if (flags < 0)
        return -1;
    return (flags & FD_CLOEXEC) == 0;
#endif
}

UTEST(pipe, ends_are_not_inherited_by_children)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(0, inheritable(r));
    EXPECT_EQ(0, inheritable(w));
    dino_pipe_close(r);
    dino_pipe_close(w);
}

#ifndef _WIN32
static int pipe_pending(void)
{
    sigset_t pending;
    sigpending(&pending);
    return sigismember(&pending, SIGPIPE);
}
#endif

// Without the fix the write raises SIGPIPE and this process ends (#82).
UTEST(pipe, write_after_the_read_end_closes_fails_without_killing_the_process)
{
#ifndef _WIN32
    struct sigaction before_action, after_action;
    sigset_t         before_mask, after_mask;
    sigaction(SIGPIPE, NULL, &before_action);
    pthread_sigmask(SIG_SETMASK, NULL, &before_mask);
#endif
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(0, dino_pipe_close(r));
    EXPECT_EQ(-1, dino_pipe_write(w, "x", 1));
    EXPECT_EQ(-1, dino_pipe_write(w, "x", 1));
    dino_pipe_close(w);
#ifndef _WIN32
    // Nothing left behind: no pending SIGPIPE, same mask, same disposition.
    EXPECT_FALSE(pipe_pending());
    sigaction(SIGPIPE, NULL, &after_action);
    pthread_sigmask(SIG_SETMASK, NULL, &after_mask);
    EXPECT_TRUE(before_action.sa_handler == after_action.sa_handler);
    EXPECT_EQ(sigismember(&before_mask, SIGPIPE), sigismember(&after_mask, SIGPIPE));
#endif
}

#ifndef _WIN32
// A SIGPIPE that was already pending (blocked by the caller) belongs to the
// caller; the write must not swallow it.
UTEST(pipe, an_earlier_pending_sigpipe_is_left_alone)
{
    sigset_t pipe_only, old;
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_only, &old);
    pthread_kill(pthread_self(), SIGPIPE);
    ASSERT_TRUE(pipe_pending());

    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    dino_pipe_close(r);
    EXPECT_EQ(-1, dino_pipe_write(w, "x", 1));
    dino_pipe_close(w);
    EXPECT_TRUE(pipe_pending());

    const struct timespec now = { 0, 0 };
    EXPECT_EQ(SIGPIPE, sigtimedwait(&pipe_only, NULL, &now));
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}
#endif

#ifndef _WIN32
// A caller that blocks SIGPIPE itself keeps it blocked, and the write's own
// SIGPIPE isn't left pending for it.
UTEST(pipe, a_caller_blocking_sigpipe_keeps_it_blocked_with_nothing_pending)
{
    sigset_t pipe_only, old, after;
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_only, &old);

    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    dino_pipe_close(r);
    EXPECT_EQ(-1, dino_pipe_write(w, "x", 1));
    dino_pipe_close(w);
    EXPECT_FALSE(pipe_pending());
    pthread_sigmask(SIG_SETMASK, NULL, &after);
    EXPECT_TRUE(sigismember(&after, SIGPIPE));
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}
#endif

// Only a read end can be waited for, on every platform: a write end would
// time out on Linux, or report readable once its reader is gone.
UTEST(pipe, wait_on_a_write_end_fails)
{
    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(-1, dino_pipe_wait(w, 10));
    dino_pipe_close(r);
    EXPECT_EQ(-1, dino_pipe_wait(w, 10));
    dino_pipe_close(w);
}

UTEST(pipe, invalid_arguments_are_rejected)
{
    char buf[4];
    EXPECT_EQ(-1, dino_pipe_read(DINO_INVALID_PIPE, buf, 4));
    EXPECT_EQ(-1, dino_pipe_write(DINO_INVALID_PIPE, buf, 4));
    EXPECT_EQ(-1, dino_pipe_wait(DINO_INVALID_PIPE, 0));
    EXPECT_EQ(-1, dino_pipe_close(DINO_INVALID_PIPE));
    dino_pipe_t only;
    EXPECT_EQ(-1, dino_pipe_create(NULL, &only));
    EXPECT_EQ(-1, dino_pipe_create(&only, NULL));

    dino_pipe_t r, w;
    ASSERT_EQ(0, dino_pipe_create(&r, &w));
    EXPECT_EQ(-1, dino_pipe_read(r, NULL, 4));
    EXPECT_EQ(-1, dino_pipe_write(w, NULL, 4));
    EXPECT_EQ(0, dino_pipe_write(w, buf, 0));
    EXPECT_EQ(0, dino_pipe_read(r, buf, 0));
    dino_pipe_close(r);
    dino_pipe_close(w);
}

UTEST_MAIN()
