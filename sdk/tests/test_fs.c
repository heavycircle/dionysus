// Before any include: 64-bit file offsets for the sparse-file setup on the
// 32-bit targets (arm, i386, ppc).
#define _FILE_OFFSET_BITS 64

#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <io.h>

#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <utest.h>

#include <dino/core/fs.h>
#include <dino/core/runtime.h>

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
    uint64_t size = 1;
    EXPECT_EQ(0, dino_file_size(path, &size));
    EXPECT_EQ(10u, size);
    remove(path);
}

UTEST(file_size, empty_file)
{
    const char *path = "dino_test_empty.bin";
    ASSERT_EQ(0, resize(path, 0));
    uint64_t size = 1;
    EXPECT_EQ(0, dino_file_size(path, &size));
    EXPECT_EQ(0u, size);
    remove(path);
}

UTEST(file_size, missing_file_fails)
{
    uint64_t size = 1;
    EXPECT_EQ(-1, dino_file_size("dino_test_does_not_exist.bin", &size));
    EXPECT_EQ(0u, size);
}

UTEST(file_size, directory_fails)
{
    uint64_t size = 1;
    EXPECT_EQ(-1, dino_file_size(".", &size));
    EXPECT_EQ(0u, size);
}

UTEST(file_size, null_arguments_fail)
{
    uint64_t size;
    EXPECT_EQ(-1, dino_file_size(NULL, &size));
    EXPECT_EQ(-1, dino_file_size("dino_test_does_not_exist.bin", NULL));
}

// 0 if the link was made, 1 if this system can't make one, -1 on any other
// failure. Windows needs developer mode or a privilege; wine (10.0) reports
// success without creating anything, so the link is checked by its attributes.
static int make_symlink(const char *target, const char *link, int is_dir)
{
#ifdef _WIN32
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (is_dir)
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (!CreateSymbolicLinkA(link, target, flags))
        return GetLastError() == ERROR_PRIVILEGE_NOT_HELD ? 1 : -1;
    DWORD attributes = GetFileAttributesA(link);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 0
                                                                                                : 1;
#else
    (void)is_dir;
    return symlink(target, link) == 0 ? 0 : -1;
#endif
}

static void remove_symlink(const char *link, int is_dir)
{
#ifdef _WIN32
    if (is_dir)
    {
        RemoveDirectoryA(link);
        return;
    }
#else
    (void)is_dir;
#endif
    remove(link);
}

// The size of the file a link points to, as stat reports it, not the link's.
UTEST(file_size, symlink_to_a_file_reports_the_file)
{
    const char *path = "dino_test_link_target.bin";
    const char *link = "dino_test_link.bin";
    ASSERT_EQ(0, resize(path, 1234));
    remove_symlink(link, 0);
    int made = make_symlink(path, link, 0);
    if (made == 1)
    {
        remove(path);
        UTEST_SKIP("this system can't create symbolic links");
    }
    ASSERT_EQ(0, made);

    uint64_t size = 0;
    EXPECT_EQ(0, dino_file_size(link, &size));
    EXPECT_EQ(1234u, size);

    // Dangling once the file is gone.
    remove(path);
    size = 1;
    EXPECT_EQ(-1, dino_file_size(link, &size));
    EXPECT_EQ(0u, size);
    remove_symlink(link, 0);
}

UTEST(file_size, symlink_to_a_directory_fails)
{
    const char *link = "dino_test_dir_link";
    remove_symlink(link, 1);
    int made = make_symlink(".", link, 1);
    if (made == 1)
        UTEST_SKIP("this system can't create symbolic links");
    ASSERT_EQ(0, made);

    uint64_t size = 1;
    EXPECT_EQ(-1, dino_file_size(link, &size));
    EXPECT_EQ(0u, size);
    remove_symlink(link, 1);
}

// Past both the 2 GiB and 4 GiB limits that dino_file_size exists for. The
// file is sparse, so it costs no disk space.
UTEST(file_size, larger_than_4_gib)
{
    const char    *path = "dino_test_large.bin";
    const uint64_t size = 5ull << 30;
    ASSERT_EQ(0, resize(path, size));
    uint64_t got = 0;
    EXPECT_EQ(0, dino_file_size(path, &got));
    EXPECT_EQ(size, got);
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
    dino_runtime_cleanup();
#ifndef _WIN32
    // Cleanup must not close stdio (#61).
    for (int fd = 0; fd <= 2; fd++)
        EXPECT_NE(-1, fcntl(fd, F_GETFD));
#endif
}

UTEST_MAIN()
