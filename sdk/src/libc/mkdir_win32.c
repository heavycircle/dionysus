#include <sys/types.h>

#include "libc_win32_internal.h"

// POSIX's mkdir(path, mode), declared by sdk/portability/win32-posix/sys/stat.h
// as a macro over this. Windows has no permission bits, so mode is ignored, as
// MinGW's chmod ignores all but the write bit.
int dino__mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *wide = dino__libc_widen(path);
    if (!wide)
        return -1;
    int ok = CreateDirectoryW(wide, NULL) != 0;
    if (!ok)
        errno = dino__libc_errno();
    free(wide);
    return ok ? 0 : -1;
}
