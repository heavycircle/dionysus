#ifndef DINO_CORE_FS_H
#define DINO_CORE_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Get the size of a regular file, including files over 2 GB on Windows,
 * where ftell and stat's size stop at 2 GB. A symbolic link reports the size
 * of the file it points to, on Windows as on Linux.
 *
 * @param  path  Path to the file, UTF-8 on every platform.
 * @param  size  Receives the size in bytes; 0 on failure.
 * @return 0 on success, -1 if the file doesn't exist, isn't a regular file
 *         (a directory, for example) or can't be examined, or for a NULL
 *         argument.
 */
int dino_file_size(const char *path, uint64_t *size);

/**
 * Initialize fs environment.
 *
 * Points any of stdin, stdout and stderr that are closed at /dev/null.
 * Processes started by the kernel's call_usermodehelper get no stdio, so
 * without this the program's own files would be opened as fds 0-2 and later
 * writes to stdout or stderr would land in them. Descriptors that are already
 * open are left alone. No-op on Windows.
 *
 * If /dev/null can't be opened, the closed descriptors are opened on "/"
 * instead: reads and writes on them fail, but the program's own files still
 * can't take their place.
 *
 * @return 0 on success, -1 if /dev/null could not be opened.
 */
int dino_fs_init(void);

#ifdef __cplusplus
}
#endif

#endif
