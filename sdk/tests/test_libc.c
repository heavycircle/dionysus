#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <utest.h>

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
    const off_t far = (off_t)5 << 30;
    EXPECT_EQ(0, fseeko(f, far, SEEK_SET));
    EXPECT_EQ(far, ftello(f));
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

UTEST_MAIN()
