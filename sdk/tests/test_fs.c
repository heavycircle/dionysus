// Before any include: 64-bit file offsets for the sparse-file setup on the
// 32-bit targets (arm, i386, ppc).
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>

#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/fs.h>
#include <dino/util/fs.h>

static int resize(const char *path, uint64_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
#ifdef _WIN32
    // Sparse first: NTFS would otherwise allocate the whole size on disk.
    DWORD unused;
    DeviceIoControl(
        (HANDLE)_get_osfhandle(_fileno(f)), FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &unused, NULL);
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

// A device isn't a regular file on either platform, and on Windows a named
// pipe is refused without being opened, which would take its only instance.
UTEST(file_size, devices_and_pipes_fail)
{
    uint64_t size = 1;
#ifdef _WIN32
    EXPECT_EQ(-1, dino_file_size("NUL", &size));
    const wchar_t *name   = L"\\\\.\\pipe\\dino-test-file-size";
    HANDLE         server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 512, 0, NULL);
    ASSERT_TRUE(server != INVALID_HANDLE_VALUE);
    EXPECT_EQ(-1, dino_file_size("\\\\.\\pipe\\dino-test-file-size", &size));
    EXPECT_EQ(-1, dino_file_size("//./PIPE/dino-test-file-size", &size));
    // The instance is still free for a real client.
    HANDLE client
        = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    EXPECT_TRUE(client != INVALID_HANDLE_VALUE);
    if (client != INVALID_HANDLE_VALUE)
        CloseHandle(client);
    CloseHandle(server);
#else
    EXPECT_EQ(-1, dino_file_size("/dev/null", &size));
#endif
    EXPECT_EQ(0u, size);
}

#ifdef _WIN32
// The reason dino_file_size opens with full sharing and no access rights: a
// file another handle holds without sharing still has a size.
UTEST(file_size, a_file_held_open_without_sharing)
{
    const char *path = "dino_test_unshared.bin";
    ASSERT_EQ(0, resize(path, 77));
    HANDLE held = CreateFileW(
        L"dino_test_unshared.bin", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_TRUE(held != INVALID_HANDLE_VALUE);
    uint64_t size = 0;
    EXPECT_EQ(0, dino_file_size(path, &size));
    EXPECT_EQ(77u, size);
    CloseHandle(held);
    remove(path);
}
#endif

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
// file is sparse, so it costs no disk space, on NTFS too.
UTEST(file_size, larger_than_4_gib)
{
    const char    *path = "dino_test_large.bin";
    const uint64_t size = 5ull << 30;
    remove(path); // left by a run that stopped early
    ASSERT_EQ(0, resize(path, size));
    uint64_t got = 0;
    EXPECT_EQ(0, dino_file_size(path, &got));
    EXPECT_EQ(size, got);
    remove(path);
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

// rename and remove are the C library's on Linux and the SDK's on Windows
// (sdk/src/libc), where the UCRT's don't replace a file or remove a read-only
// one or a directory.

// The save pattern: write a temporary file, rename it over the old one.
UTEST(rename, replaces_an_existing_file)
{
    ASSERT_EQ(0, write_file("dino_rename_old.txt", "old"));
    ASSERT_EQ(0, write_file("dino_rename_new.txt", "newer"));
    EXPECT_EQ(0, rename("dino_rename_new.txt", "dino_rename_old.txt"));
    uint64_t size = 0;
    EXPECT_EQ(0, dino_file_size("dino_rename_old.txt", &size));
    EXPECT_EQ(5u, size);
    EXPECT_EQ(-1, dino_file_size("dino_rename_new.txt", &size));
    remove("dino_rename_old.txt");
}

UTEST(rename, a_missing_source_sets_enoent)
{
    errno = 0;
    EXPECT_EQ(-1, rename("dino_rename_missing.txt", "dino_rename_to.txt"));
    EXPECT_EQ(ENOENT, errno);
}

UTEST(remove, removes_a_file_and_an_empty_directory)
{
    ASSERT_EQ(0, write_file("dino_remove.txt", "x"));
    EXPECT_EQ(0, remove("dino_remove.txt"));
    uint64_t size;
    EXPECT_EQ(-1, dino_file_size("dino_remove.txt", &size));
    errno = 0;
    EXPECT_EQ(-1, remove("dino_remove.txt"));
    EXPECT_EQ(ENOENT, errno);
#ifdef _WIN32
    ASSERT_TRUE(CreateDirectoryW(L"dino_remove_dir", NULL));
#else
    ASSERT_EQ(0, mkdir("dino_remove_dir", 0700));
#endif
    EXPECT_EQ(0, remove("dino_remove_dir"));
}

UTEST(remove, a_directory_with_files_sets_enotempty)
{
#ifdef _WIN32
    ASSERT_TRUE(CreateDirectoryW(L"dino_remove_full", NULL));
#else
    ASSERT_EQ(0, mkdir("dino_remove_full", 0700));
#endif
    ASSERT_EQ(0, write_file("dino_remove_full/a.txt", "x"));
    errno = 0;
    EXPECT_EQ(-1, remove("dino_remove_full"));
    EXPECT_EQ(ENOTEMPTY, errno);
    EXPECT_EQ(0, remove("dino_remove_full/a.txt"));
    EXPECT_EQ(0, remove("dino_remove_full"));
}

#ifdef _WIN32
// Linux only asks whether the directory is writable; Windows' DeleteFile
// refuses a read-only file unless the attribute is cleared first.
UTEST(remove, removes_a_read_only_file)
{
    ASSERT_EQ(0, write_file("dino_remove_ro.txt", "x"));
    ASSERT_TRUE(SetFileAttributesW(L"dino_remove_ro.txt", FILE_ATTRIBUTE_READONLY));
    EXPECT_EQ(0, remove("dino_remove_ro.txt"));
    EXPECT_EQ(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(L"dino_remove_ro.txt"));
}
#endif

// Plain fopen, rename and the SDK agree on a non-ASCII path everywhere: on
// Windows through the UTF-8 manifest dino_test() links.
UTEST(rename, a_utf8_path_means_the_same_to_fopen_and_the_sdk)
{
#ifdef _WIN32
    ASSERT_EQ((UINT)CP_UTF8, GetACP());
#endif
    const char *name    = "dino_caf\xc3\xa9.txt";
    const char *renamed = "dino_\xc3\xbc\x62\x65r.txt";
    ASSERT_EQ(0, write_file(name, "abc"));
    uint64_t size = 0;
    EXPECT_EQ(0, dino_file_size(name, &size));
    EXPECT_EQ(3u, size);
#ifdef _WIN32
    EXPECT_NE(INVALID_FILE_ATTRIBUTES, GetFileAttributesW(L"dino_café.txt"));
#endif
    EXPECT_EQ(0, rename(name, renamed));
    FILE *f = fopen(renamed, "rb");
    EXPECT_TRUE(f != NULL);
    if (f)
        fclose(f);
    EXPECT_EQ(0, remove(renamed));
}

UTEST(fs_init, keeps_stdio_open_and_is_repeatable)
{
    EXPECT_EQ(0, dino_fs_init());
    EXPECT_EQ(0, dino_fs_init());
#ifndef _WIN32
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

/* ---- mkdir and dino_mkdir_p ---- */

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// POSIX's two-argument mkdir, on Windows too, with errno as on Linux.
UTEST(mkdir, takes_a_mode_and_sets_errno)
{
    rmdir("dino_mkdir_one");
    ASSERT_EQ(0, mkdir("dino_mkdir_one", 0755));
    EXPECT_TRUE(is_dir("dino_mkdir_one"));
    errno = 0;
    EXPECT_EQ(-1, mkdir("dino_mkdir_one", 0755));
    EXPECT_EQ(EEXIST, errno);
    errno = 0;
    EXPECT_EQ(-1, mkdir("dino_mkdir_missing/child", 0755));
    EXPECT_EQ(ENOENT, errno);
    EXPECT_EQ(0, rmdir("dino_mkdir_one"));
}

UTEST(mkdir, a_utf8_name)
{
    const char *name = "dino_r\xc3\xa9pertoire";
    rmdir(name);
    ASSERT_EQ(0, mkdir(name, 0700));
    EXPECT_TRUE(is_dir(name));
    EXPECT_EQ(0, rmdir(name));
}

// A part of the path that is a file: ENOTDIR on Linux, and on Windows too,
// which says "path not found" itself.
UTEST(mkdir, a_file_on_the_way_is_enotdir)
{
    ASSERT_EQ(0, write_file("dino_mkdir_file", "x"));
    errno = 0;
    EXPECT_EQ(-1, mkdir("dino_mkdir_file/sub", 0755));
    EXPECT_EQ(ENOTDIR, errno);
    remove("dino_mkdir_file");
}

#ifdef _WIN32
// MinGW's own one-argument form still builds, through the same UTF-8 path.
UTEST(mkdir, the_one_argument_form_works_on_windows)
{
    rmdir("dino_mkdir_1arg");
    ASSERT_EQ(0, mkdir("dino_mkdir_1arg"));
    EXPECT_TRUE(is_dir("dino_mkdir_1arg"));
    EXPECT_EQ(0, rmdir("dino_mkdir_1arg"));
}

// A \\?\ path: the prefix and the drive are the root, never created.
UTEST(mkdir_p, a_path_with_the_long_path_prefix)
{
    char tmp[1024], path[1200];
    ASSERT_GT(dino_path_temp(tmp, sizeof(tmp)), 0);
    for (char *c = tmp; *c; c++)
        if (*c == '/')
            *c = '\\';
    snprintf(path, sizeof(path), "\\\\?\\%s\\dino_mkp_long\\x", tmp);
    ASSERT_EQ(0, dino_mkdir_p(path, 0755));
    EXPECT_TRUE(is_dir(path + 4));
    EXPECT_EQ(0, rmdir(path + 4));
    snprintf(path, sizeof(path), "%s\\dino_mkp_long", tmp);
    EXPECT_EQ(0, rmdir(path));
}
#else
// As mkdir -p -m: only the last directory gets the mode as given; the ones on
// the way stay writable for their owner, so the next could be made in them.
UTEST(mkdir_p, only_the_last_directory_gets_a_read_only_mode)
{
    ASSERT_EQ(0, dino_mkdir_p("dino_mkp_ro/a/b", 0555));
    struct stat st;
    ASSERT_EQ(0, stat("dino_mkp_ro/a", &st));
    EXPECT_TRUE(st.st_mode & S_IWUSR);
    ASSERT_EQ(0, stat("dino_mkp_ro/a/b", &st));
    EXPECT_FALSE(st.st_mode & S_IWUSR);
    EXPECT_EQ(0, rmdir("dino_mkp_ro/a/b"));
    EXPECT_EQ(0, rmdir("dino_mkp_ro/a"));
    EXPECT_EQ(0, rmdir("dino_mkp_ro"));
}
#endif

// A success leaves dino_error() as it was, though mkdir failed on the parts
// that were already there.
UTEST(mkdir_p, success_keeps_the_last_error)
{
    ASSERT_EQ(0, dino_mkdir_p("dino_mkp_keep/a", 0755));
    uint64_t size;
    EXPECT_EQ(-1, dino_file_size(NULL, &size));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(0, dino_mkdir_p("dino_mkp_keep/a", 0755));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    rmdir("dino_mkp_keep/a");
    rmdir("dino_mkp_keep");
}

UTEST(mkdir_p, creates_every_missing_level)
{
    rmdir("dino_mkp/a/b/c");
    rmdir("dino_mkp/a/b");
    rmdir("dino_mkp/a");
    rmdir("dino_mkp");
    ASSERT_EQ(0, dino_mkdir_p("dino_mkp/a/b/c", 0755));
    EXPECT_TRUE(is_dir("dino_mkp/a/b/c"));
    // Already there, with a trailing separator and a doubled one.
    EXPECT_EQ(0, dino_mkdir_p("dino_mkp/a/b/c/", 0755));
    EXPECT_EQ(0, dino_mkdir_p("dino_mkp//a", 0755));
#ifdef _WIN32
    EXPECT_EQ(0, dino_mkdir_p("dino_mkp\\a\\b\\d", 0755));
    EXPECT_TRUE(is_dir("dino_mkp/a/b/d"));
    rmdir("dino_mkp/a/b/d");
#endif
    EXPECT_EQ(0, rmdir("dino_mkp/a/b/c"));
    EXPECT_EQ(0, rmdir("dino_mkp/a/b"));
    EXPECT_EQ(0, rmdir("dino_mkp/a"));
    EXPECT_EQ(0, rmdir("dino_mkp"));
}

// An absolute path, from the temporary directory: the root (/, C:\) is never
// created.
UTEST(mkdir_p, an_absolute_path)
{
    char tmp[1024], path[1200];
    ASSERT_GT(dino_path_temp(tmp, sizeof(tmp)), 0);
    snprintf(path, sizeof(path), "%s/dino_mkp_abs/x", tmp);
    ASSERT_EQ(0, dino_mkdir_p(path, 0755));
    EXPECT_TRUE(is_dir(path));
    EXPECT_EQ(0, rmdir(path));
    snprintf(path, sizeof(path), "%s/dino_mkp_abs", tmp);
    EXPECT_EQ(0, rmdir(path));
}

UTEST(mkdir_p, a_file_in_the_way_fails)
{
    ASSERT_EQ(0, write_file("dino_mkp_file", "x"));
    EXPECT_EQ(-1, dino_mkdir_p("dino_mkp_file", 0755));
    EXPECT_EQ(DINO_EEXIST, dino_error());
    EXPECT_EQ(-1, dino_mkdir_p("dino_mkp_file/sub", 0755));
    remove("dino_mkp_file");
    EXPECT_EQ(-1, dino_mkdir_p("", 0755));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino_mkdir_p(NULL, 0755));
}

/* ---- dino_path_temp and dino_path_executable ---- */

UTEST(path, temp_is_a_directory_without_a_trailing_separator)
{
    char buf[1024];
    int  len = dino_path_temp(buf, sizeof(buf));
    ASSERT_GT(len, 0);
    ASSERT_LT(len, (int)sizeof(buf));
    EXPECT_EQ((size_t)len, strlen(buf));
    EXPECT_TRUE(is_dir(buf));
    EXPECT_NE('/', buf[len - 1]);
    EXPECT_NE('\\', buf[len - 1]);
}

// The variable each platform reads first: TMPDIR, or TMP on Windows.
UTEST(path, temp_follows_the_environment)
{
#ifdef _WIN32
    const char *var = "TMP";
#else
    const char *var = "TMPDIR";
#endif
    char before[1024], dir[1200], buf[1200];
    ASSERT_GT(dino_path_temp(before, sizeof(before)), 0);
    snprintf(dir, sizeof(dir), "%s/dino_path_tmp", before);
    mkdir(dir, 0700);
    const char *old   = getenv(var);
    char       *saved = old ? strdup(old) : NULL;
    char        with_slash[1210];
    snprintf(with_slash, sizeof(with_slash), "%s/", dir);
    ASSERT_EQ(0, setenv(var, with_slash, 1));
    int len = dino_path_temp(buf, sizeof(buf));
    if (saved)
        setenv(var, saved, 1);
    else
        unsetenv(var);
    free(saved);
    rmdir(dir);
    ASSERT_GT(len, 0);
#ifdef _WIN32
    // GetTempPathW gives back a full path with backslashes.
    for (char *c = dir; *c; c++)
        if (*c == '/')
            *c = '\\';
    for (char *c = buf; *c; c++)
        if (*c == '/')
            *c = '\\';
    EXPECT_EQ(0, _stricmp(dir, buf));
#else
    EXPECT_STREQ(dir, buf);
#endif
}

UTEST(path, executable_is_this_program)
{
    char buf[1024];
    int  len = dino_path_executable(buf, sizeof(buf));
    ASSERT_GT(len, 0);
    ASSERT_LT(len, (int)sizeof(buf));
    EXPECT_EQ((size_t)len, strlen(buf));
    EXPECT_NE(NULL, strstr(buf, "test_fs"));
    struct stat st;
    EXPECT_EQ(0, stat(buf, &st));
}

// As snprintf: what fits, NUL-terminated, and the full length back.
UTEST(path, a_short_buffer_gets_a_cut_path_and_the_full_length)
{
    char full[1024], small[8];
    int  len = dino_path_executable(full, sizeof(full));
    ASSERT_GT(len, 8);
    EXPECT_EQ(len, dino_path_executable(small, sizeof(small)));
    EXPECT_EQ(7u, strlen(small));
    EXPECT_EQ(0, strncmp(full, small, 7));
    EXPECT_EQ(len, dino_path_executable(NULL, 0));
    EXPECT_EQ(dino_path_temp(full, sizeof(full)), dino_path_temp(NULL, 0));
    EXPECT_EQ(-1, dino_path_temp(NULL, 4));
}

UTEST_MAIN()
