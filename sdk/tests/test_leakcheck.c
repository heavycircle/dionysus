// The leak check that dino_test() links on the Linux targets
// (sdk/testing/leakcheck.c) must not fail a test for what libc keeps, nor lose
// count of an allocation. Every case here leaves nothing of its own behind, so
// this executable passes only if the check agrees. tests/cli.sh has the other
// half: real leaks are still reported.
#ifndef _WIN32
#define _GNU_SOURCE
#include <grp.h>
#include <malloc.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include <utest.h>

#ifdef _WIN32

UTEST(leakcheck, linux_targets_only)
{
    EXPECT_TRUE(1);
}

#else

static volatile int g_handlers_run;

static void handler(void)
{
    g_handlers_run = g_handlers_run + 1;
}

// libc's list of exit handlers grows by an allocated block after the first 32.
UTEST(leakcheck, more_exit_handlers_than_the_first_block)
{
    for (int i = 0; i < 100; i++)
        ASSERT_EQ(0, atexit(handler));
}

UTEST(leakcheck, every_allocator_is_counted_once)
{
    void *p = memalign(64, 100);
    ASSERT_TRUE(p != NULL);
    free(p);

    p = valloc(100);
    ASSERT_TRUE(p != NULL);
    free(p);

    p = aligned_alloc(64, 128);
    ASSERT_TRUE(p != NULL);
    free(p);

    ASSERT_EQ(0, posix_memalign(&p, 64, 100));
    free(p);

    p = reallocarray(NULL, 4, 8);
    ASSERT_TRUE(p != NULL);
    void *grown = reallocarray(p, 4000, 8);
    ASSERT_TRUE(grown != NULL);
    free(grown);

    p = calloc(4, 8);
    ASSERT_TRUE(p != NULL);
    grown = realloc(p, 4000);
    ASSERT_TRUE(grown != NULL);
    free(grown);
}

// The entries these return live in buffers libc keeps.
UTEST(leakcheck, user_and_group_lookups)
{
    EXPECT_TRUE(getpwuid(0) != NULL);
    EXPECT_TRUE(getgrgid(0) != NULL);

    int entries = 0;
    setpwent();
    while (getpwent())
        entries++;
    endpwent();
    EXPECT_GT(entries, 0);
}

// libc copies TZ into an allocated buffer once it is 32 characters or longer.
// On the 32-bit targets these calls go by other names (__localtime64, ...).
UTEST(leakcheck, long_time_zone_name)
{
    // libc's own setenv, whose copy is what the check must count.
    ASSERT_EQ(0,
        DINO_PLATFORM_CALL(
            setenv("TZ", "/usr/share/zoneinfo/America/Argentina/ComodRivadavia", 1)));
    time_t    now = time(NULL);
    struct tm tm;
    char      text[64];
    tzset();
    EXPECT_TRUE(localtime(&now) != NULL);
    EXPECT_TRUE(localtime_r(&now, &tm) != NULL);
    EXPECT_NE((time_t)-1, mktime(&tm));
    EXPECT_TRUE(ctime(&now) != NULL);
    EXPECT_TRUE(ctime_r(&now, text) != NULL);
    EXPECT_GT(strftime(text, sizeof(text), "%Z %z", &tm), 0u);
}

// A forked child starts with the parent's count; it must not report it.
UTEST(leakcheck, forked_child_exits_cleanly)
{
    void *held = malloc(10);
    ASSERT_TRUE(held != NULL);
    pid_t pid = DINO_PLATFORM_CALL(fork());
    ASSERT_NE(-1, pid);
    if (pid == 0)
        exit(0);
    int   status = -1;
    pid_t waited = DINO_PLATFORM_CALL(waitpid(pid, &status, 0));
    ASSERT_EQ(pid, waited);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(0, WEXITSTATUS(status));
    free(held);
}

#endif

UTEST_MAIN()
