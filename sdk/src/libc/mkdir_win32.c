#include <wchar.h>

#include "libc_win32_internal.h"

// Windows says "path not found" where a component of the path is a file;
// Linux says ENOTDIR. The nearest part of the path that exists tells them
// apart.
static int missing_parent_errno(wchar_t *wide)
{
    for (size_t n = wcslen(wide); n > 0; n--)
    {
        if (wide[n - 1] != L'\\' && wide[n - 1] != L'/')
            continue;
        wide[n - 1]      = L'\0';
        DWORD attributes = GetFileAttributesW(wide);
        if (attributes != INVALID_FILE_ATTRIBUTES)
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? ENOENT : ENOTDIR;
    }
    return ENOENT;
}

// POSIX's mkdir(path, mode), declared by sdk/portability/win32-posix/sys/stat.h
// as a macro over this. Windows has no permission bits, so mode is ignored, as
// MinGW's chmod ignores all but the write bit. int, not mode_t: the header
// can't name mode_t under NO_OLDNAMES.
int dino__mkdir(const char *path, int mode)
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
    {
        int code = dino__libc_errno();
        errno    = code == ENOENT ? missing_parent_errno(wide) : code;
    }
    free(wide);
    return ok ? 0 : -1;
}

// mkdir(path), MinGW's own form, through the same UTF-8 path.
int dino__mkdir_path(const char *path)
{
    return dino__mkdir(path, 0777);
}
