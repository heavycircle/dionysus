#ifndef DINO_UTIL_FS_H
#define DINO_UTIL_FS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Create a directory and every missing directory above it, as `mkdir -p`
 * does. Since 1.0.
 *
 * A directory that already exists is fine, the last one included. Paths are
 * UTF-8; on Windows both / and \ separate, and a drive (C:\) or share
 * (\\server\share\) at the start is taken as it is.
 *
 * @param  path  Directory to create.
 * @param  mode  Permissions for the last directory, less the umask, as for
 *               mkdir. The ones created on the way get mode plus owner write
 *               and search, as with `mkdir -p -m`, so the next can be made in
 *               them. Ignored on Windows, which has no permission bits.
 * @return 0 on success, -1 on error. dino_error() (dino/core/error.h) is
 *         DINO_EEXIST when a file that isn't a directory is in the way.
 */
int dino_mkdir_p(const char *path, mode_t mode);

#ifdef __cplusplus
}
#endif

#endif // DINO_UTIL_FS_H
