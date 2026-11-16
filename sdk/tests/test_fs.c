// Before any include: 64-bit file offsets for the sparse-file setup on the
// 32-bit targets (arm, i386, ppc).
#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <utest.h>

#include <dino/core/fs.h>

static int resize(const char *path, uint64_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
#ifdef _WIN32
    int rc = _chsize_s(_fileno(f), (long long)size) == 0 ? 0 : -1;
#else
    int rc = ftruncate(fileno(f), (off_t)size);
#endif
    fclose(f);
    return rc;
}

UTEST(file_size, small_file)
{
    const char *path = "dino_test_small.bin";
    FILE       *f    = fopen(path, "wb");
    ASSERT_TRUE(f != NULL);
    fwrite("0123456789", 1, 10, f);
    fclose(f);
    EXPECT_EQ(10u, dino_file_size(path));
    remove(path);
}

UTEST(file_size, empty_file)
{
    const char *path = "dino_test_empty.bin";
    ASSERT_EQ(0, resize(path, 0));
    EXPECT_EQ(0u, dino_file_size(path));
    remove(path);
}

UTEST(file_size, missing_file_is_zero)
{
    EXPECT_EQ(0u, dino_file_size("dino_test_does_not_exist.bin"));
}

// Past both the 2 GiB and 4 GiB limits that dino_file_size exists for. The
// file is sparse, so it costs no disk space.
UTEST(file_size, larger_than_4_gib)
{
    const char    *path = "dino_test_large.bin";
    const uint64_t size = 5ull << 30;
    ASSERT_EQ(0, resize(path, size));
    EXPECT_EQ(size, dino_file_size(path));
    remove(path);
}

UTEST(fs_init, keeps_stdio_open_and_is_repeatable)
{
    EXPECT_EQ(0, dino_fs_init());
    EXPECT_EQ(0, dino_fs_init());
#ifndef _WIN32
    for (int fd = 0; fd <= 2; fd++)
        EXPECT_NE(-1, fcntl(fd, F_GETFD));
#endif
    dino_fs_cleanup();
#ifndef _WIN32
    // Cleanup must not close stdio (#61).
    for (int fd = 0; fd <= 2; fd++)
        EXPECT_NE(-1, fcntl(fd, F_GETFD));
#endif
}

UTEST_MAIN()
