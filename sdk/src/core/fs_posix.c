#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dino/core/fs.h>

uint64_t dino_file_size(const char *filepath)
{
    struct stat st;

    if (stat(filepath, &st) != 0)
    {
        return 0;
    }

    return (uint64_t)st.st_size;
}

int dino_fs_init(void)
{
    // Only fill descriptors that are missing: stdio the program was started
    // with, even just stdout or stderr, must survive.
    for (int fd = 0; fd <= 2; fd++)
    {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            continue;

        int null = open("/dev/null", O_RDWR);
        if (null < 0)
            return -1;
        if (null != fd)
        {
            dup2(null, fd);
            close(null);
        }
    }

    return 0;
}

// Stdio belongs to the program, so there is nothing to undo.
void dino_fs_cleanup(void)
{
}
