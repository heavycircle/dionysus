#ifndef MODULES_FRONTEND_FILEIO_H
#define MODULES_FRONTEND_FILEIO_H

#include <modules/runtime/arch/syscall.h>

int open(const char *pathname, int flags);

int close(int fd);

#endif // MODULES_FRONTEND_FILEIO_H
