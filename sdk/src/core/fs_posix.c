#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <dino/core/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "error_internal.h"

int dino_file_size(const char *path, uint64_t *size)
{
    if (size)
        *size = 0;
    struct stat st;
    if (!path || !size)
        return dino__fail(DINO_EINVAL);
    if (stat(path, &st) != 0)
        return dino__fail_os();
    // A directory, a device, a pipe: there, but with no size to report.
    if (!S_ISREG(st.st_mode))
        return dino__fail(DINO_EINVAL);
    *size = (uint64_t)st.st_size;
    return 0;
}

// dino_fs_init with the device to use, so that a test can take it away.
// Internal, not API (the dino__ prefix, CONTRIBUTING.md#design-rules).
int dino__fs_init_with(const char *null_device)
{
    int rc = 0;
    // Only fill descriptors that are missing: stdio the program was started
    // with, even just stdout or stderr, must survive.
    for (int fd = 0; fd <= 2; fd++)
    {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            continue;

        int filler = open(null_device, O_RDWR);
        if (filler < 0)
        {
            // No /dev yet, or a chroot without one. The descriptor must still
            // be taken, or the program's next file gets it. A directory can
            // be opened anywhere, and reads and writes on it fail, where a
            // pipe would block or raise SIGPIPE.
            rc     = dino__fail_os();
            filler = open("/", O_RDONLY);
            if (filler < 0)
                return dino__fail_os();
        }
        if (filler != fd)
        {
            dup2(filler, fd);
            close(filler);
        }
    }

    return rc;
}

int dino_fs_init(void)
{
    return dino__fs_init_with("/dev/null");
}

// As snprintf: as much as fits, NUL-terminated, and the full length back.
static int copy_out(char *buf, size_t size, const char *text, size_t len)
{
    if (len > (size_t)INT_MAX)
        return dino__fail(DINO_EINVAL);
    if (size > 0)
        snprintf(buf, size, "%.*s", (int)len, text);
    return (int)len;
}

int dino_path_temp(char *buf, size_t size)
{
    // An empty string on every failure, as the SDK's out-parameters are.
    if (buf && size)
        buf[0] = '\0';
    if (!buf && size)
        return dino__fail(DINO_EINVAL);
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/')
        len--;
    return copy_out(buf, size, dir, len);
}

int dino_path_executable(char *buf, size_t size)
{
    // An empty string on every failure, as the SDK's out-parameters are.
    if (buf && size)
        buf[0] = '\0';
    if (!buf && size)
        return dino__fail(DINO_EINVAL);
    // readlink doesn't say whether it cut the path short: grow until it fits.
    for (size_t cap = 256; cap <= 65536; cap *= 2)
    {
        char *path = malloc(cap);
        if (!path)
            return dino__fail(DINO_ENOMEM);
        ssize_t len = readlink("/proc/self/exe", path, cap);
        if (len < 0)
        {
            int code = dino__error_from_os();
            free(path);
            return dino__fail(code);
        }
        if ((size_t)len < cap)
        {
            int rc = copy_out(buf, size, path, (size_t)len);
            free(path);
            return rc;
        }
        free(path);
    }
    return dino__fail(DINO_EINVAL);
}
