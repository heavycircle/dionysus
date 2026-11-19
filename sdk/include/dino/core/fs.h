#ifndef DINO_CORE_FS_H
#define DINO_CORE_FS_H

#include <stdint.h>

/**
 * Get the size of a file.
 *
 * Supports large files up to the maximum size of uint64_t.
 *
 * @param  filepath     Path to the file.
 * @return Size of the file.
 */
uint64_t dino_file_size(const char *filepath);

/**
 * Initialize fs environment.
 *
 * Points any of stdin, stdout and stderr that are closed at /dev/null.
 * Processes started by the kernel's call_usermodehelper get no stdio, so
 * without this the program's own files would be opened as fds 0-2 and later
 * writes to stdout or stderr would land in them. Descriptors that are already
 * open are left alone. No-op on Windows.
 *
 * @return 0 on success, -1 if /dev/null could not be opened.
 */
int dino_fs_init(void);

/**
 * Teardown file system environment. Leaves stdio open.
 */
void dino_fs_cleanup(void);

#endif
