#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <dino/core/fs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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
