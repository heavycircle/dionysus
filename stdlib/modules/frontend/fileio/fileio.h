#ifndef MODULES_FRONTEND_FILEIO_H
#define MODULES_FRONTEND_FILEIO_H

#include "macros.h"

#include <modules/runtime/runtime.h>
#include <stddef.h>
#include <sys/types.h>

int open(const char *pathname, int flags);

ssize_t write(int fd, const void *buf, size_t count);

int close(int fd);

#endif // MODULES_FRONTEND_FILEIO_H
