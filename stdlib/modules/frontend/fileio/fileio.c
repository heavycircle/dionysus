#include "fileio.h"

int open(const char *pathname, int flags)
{
    return __syscall2(__NR_open, (long)pathname, flags);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return __syscall3(__NR_write, fd, (long)buf, count);
}

int close(int fd)
{
    return __syscall1(__NR_close, fd);
}
