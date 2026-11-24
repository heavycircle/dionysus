#include <stdio.h>

#include "libc_win32_internal.h"

// The UCRT's remove only deletes files, and not read-only ones. POSIX remove
// also removes an empty directory, and a file's read-only bit doesn't stop it.
int remove(const char *path)
{
    if (!path)
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *wide = dino__libc_widen(path);
    if (!wide)
        return -1;
    DWORD attributes = GetFileAttributesW(wide);
    BOOL  ok;
    if (attributes == INVALID_FILE_ATTRIBUTES)
        ok = FALSE;
    else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        ok = RemoveDirectoryW(wide);
    else
    {
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        ok = DeleteFileW(wide);
        // Left as it was if the file stays.
        if (!ok && (attributes & FILE_ATTRIBUTE_READONLY))
        {
            DWORD err = GetLastError();
            SetFileAttributesW(wide, attributes);
            SetLastError(err);
        }
    }
    if (!ok)
        errno = dino__libc_errno();
    free(wide);
    return ok ? 0 : -1;
}
