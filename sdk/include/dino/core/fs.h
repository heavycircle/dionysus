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
 * Rename a file or directory, replacing a file already at the new path, as
 * rename(2) does on Linux. The C library's rename() on Windows fails when the
 * destination exists, so the usual "write a temporary file, then rename it
 * over the old one" fails from the second save on.
 *
 * Within one file system only. On Windows neither path may be open in another
 * program unless it opened the file with delete sharing.
 *
 * @param  from  Path to rename, UTF-8 on every platform.
 * @param  to    New path, UTF-8 on every platform.
 * @return 0 on success, -1 on failure or for a NULL argument.
 */
int dino_file_rename(const char *from, const char *to);

/**
 * Remove a file, or an empty directory, as remove(3) does on Linux. On
 * Windows a read-only file is removed too, as on Linux, where only the
 * directory's permissions count.
 *
 * On Windows a file that is open, in this program or another, can't be
 * removed unless it was opened with delete sharing; fopen() doesn't. Close it
 * first.
 *
 * @param  path  Path to remove, UTF-8 on every platform.
 * @return 0 on success, -1 on failure (DINO_ENOENT when it doesn't exist) or
 *         for a NULL argument.
 */
int dino_file_remove(const char *path);

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
