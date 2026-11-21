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
#include <sys/stat.h>
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

#ifndef _WIN32
// The descriptor a program started without stdio would be missing. The saved
// copy puts it back, so the rest of the run still has its stdin and stderr;
// it is kept clear of 0-2, where a plain dup() would put it.
static int close_keeping_a_copy(int fd)
{
    int saved = fcntl(fd, F_DUPFD, 10);
    DINO_PLATFORM_CALL(close(fd));
    return saved;
}

static void restore(int fd, int saved)
{
    DINO_PLATFORM_CALL(dup2(saved, fd));
    DINO_PLATFORM_CALL(close(saved));
}

static int is_dev_null(int fd)
{
    struct stat st, null;
    return fstat(fd, &st) == 0 && stat("/dev/null", &null) == 0 && S_ISCHR(st.st_mode)
        && st.st_rdev == null.st_rdev;
}

UTEST(fs_init, points_closed_stdio_at_dev_null_and_leaves_open_stdio_alone)
{
    struct stat out_before, out_after;
    ASSERT_EQ(0, fstat(1, &out_before));
    int in  = close_keeping_a_copy(0);
    int err = close_keeping_a_copy(2);
    ASSERT_NE(-1, in);
    ASSERT_NE(-1, err);
    ASSERT_EQ(-1, fcntl(0, F_GETFD));
    ASSERT_EQ(-1, fcntl(2, F_GETFD));

    int rc           = dino_fs_init();
    int in_is_null   = is_dev_null(0);
    int err_is_null  = is_dev_null(2);
    int out_is_there = fstat(1, &out_after);
    restore(0, in);
    restore(2, err);

    EXPECT_EQ(0, rc);
    EXPECT_TRUE(in_is_null);
    EXPECT_TRUE(err_is_null);
    ASSERT_EQ(0, out_is_there);
    EXPECT_TRUE(out_before.st_dev == out_after.st_dev && out_before.st_ino == out_after.st_ino);
}

// Internal: dino_fs_init with the device to use.
int dino__fs_init_with(const char *null_device);

// No /dev/null (early boot, a bare chroot): the descriptor is still taken, so
// the program's next file can't become stdin, but nothing can be read from it.
UTEST(fs_init, without_dev_null_the_descriptor_is_still_taken)
{
    int in = close_keeping_a_copy(0);
    ASSERT_NE(-1, in);

    int     rc    = dino__fs_init_with("/dino_test_no_such_directory/null");
    int     taken = fcntl(0, F_GETFD);
    char    byte;
    ssize_t got = DINO_PLATFORM_CALL(read(0, &byte, 1));
    restore(0, in);

    EXPECT_EQ(-1, rc);
    EXPECT_NE(-1, taken);
    EXPECT_EQ(-1, got);
}
#endif

UTEST_MAIN()
