#include "fileio.h"

int open(const char *pathname, int flags)
{
    return __syscall2(__NR_open, (long)pathname, flags);
}

int close(int fd)
{
    return __syscall1(__NR_close, fd);
}
