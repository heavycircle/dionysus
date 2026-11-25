#ifndef DINO_CORE_FS_H
#define DINO_CORE_FS_H

#include <stddef.h>
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
 * The directory for temporary files: $TMPDIR, or /tmp, on POSIX; what
 * GetTempPathW says on Windows (%TMP%, %TEMP%, or the user's own). UTF-8,
 * without a trailing separator. Since 0.10.
 *
 * Like snprintf: writes as much as fits in buf, always NUL-terminated when
 * size > 0, and returns the full length, so a result >= size means it was cut.
 * On failure buf is an empty string.
 *
 * @param  buf   Destination; may be NULL when size is 0.
 * @param  size  Size of buf.
 * @return The path's length without the NUL, or -1 on error.
 */
int dino_path_temp(char *buf, size_t size);

/**
 * The full path of the running program's executable: /proc/self/exe on Linux
 * (under qemu-user too, which reports the program, not itself),
 * GetModuleFileNameW on Windows. UTF-8. Since 0.10.
 *
 * Returns as dino_path_temp does.
 *
 * @param  buf   Destination; may be NULL when size is 0.
 * @param  size  Size of buf.
 * @return The path's length without the NUL, or -1 on error.
 */
int dino_path_executable(char *buf, size_t size);

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
