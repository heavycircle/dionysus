#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utest.h>

#include <dino/core/process.h>

// What the toolchain makes the C library agree on, so docs/sdk/libc.md can
// send users to it instead of a dino_* call.

_Static_assert(sizeof(off_t) == 8, "off_t is 64-bit on every target");
_Static_assert(sizeof(((struct stat *)0)->st_size) == 8, "st_size is 64-bit on every target");

#define BIG_FILE "test_libc_big.bin"

UTEST(libc, fseeko_and_ftello_go_past_4_gb)
{
    FILE *f = fopen(BIG_FILE, "wb");
    ASSERT_TRUE(f != NULL);
    // Seeking past the end writes nothing, so the file stays empty.
    const off_t beyond_4_gb = (off_t)5 << 30;
    EXPECT_EQ(0, fseeko(f, beyond_4_gb, SEEK_SET));
    EXPECT_EQ(beyond_4_gb, ftello(f));
    fclose(f);
    remove(BIG_FILE);
}

UTEST(libc, gmtime_r_and_localtime_r_are_declared)
{
    const time_t when = 1700000000; // 2023-11-14 22:13:20 UTC
    struct tm    tm;
    memset(&tm, 0, sizeof(tm));
    ASSERT_TRUE(gmtime_r(&when, &tm) == &tm);
    EXPECT_EQ(123, tm.tm_year);
    EXPECT_EQ(10, tm.tm_mon);
    EXPECT_EQ(14, tm.tm_mday);
    EXPECT_EQ(22, tm.tm_hour);
    EXPECT_EQ(13, tm.tm_min);
    EXPECT_EQ(20, tm.tm_sec);
    struct tm local;
    EXPECT_TRUE(localtime_r(&when, &local) == &local);
}

UTEST(libc, getpid_is_this_process)
{
    EXPECT_GT(getpid(), 0);
#ifdef _WIN32
    unsigned long id = 0;
    DINO_PLATFORM_CALL(id = GetCurrentProcessId());
    EXPECT_EQ(id, (unsigned long)getpid());
#else
    const char *const     argv[] = { "/bin/sh", "-c", "echo $PPID", NULL };
    dino_process_result_t r;
    ASSERT_EQ(0, dino_process_run(argv, NULL, &r));
    EXPECT_EQ((unsigned long)getpid(), strtoul(r.out, NULL, 10));
    dino_process_result_free(&r);
#endif
}

UTEST(libc, timespec_get_reads_the_wall_clock)
{
    time_t          before = time(NULL);
    struct timespec now    = { 0, 0 };
    ASSERT_EQ(TIME_UTC, timespec_get(&now, TIME_UTC));
    time_t after = time(NULL);
    EXPECT_GE(now.tv_sec, before);
    EXPECT_LE(now.tv_sec, after);
    EXPECT_LT(now.tv_nsec, 1000000000L);
}

UTEST_MAIN()
