#include <stdio.h>

#include "libc_win32_internal.h"

// The UCRT's rename refuses to replace an existing file, which POSIX rename
// does atomically. No MOVEFILE_COPY_ALLOWED: across volumes it copies, which
// isn't a rename and isn't atomic; Linux fails there too (EXDEV).
int rename(const char *from, const char *to)
{
    if (!from || !to)
    {
        errno = EINVAL;
        return -1;
    }
    wchar_t *wfrom = dino__libc_widen(from);
    if (!wfrom)
        return -1;
    wchar_t *wto = dino__libc_widen(to);
    if (!wto)
    {
        free(wfrom);
        return -1;
    }
    BOOL ok = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        errno = dino__libc_errno();
    free(wto);
    free(wfrom);
    return ok ? 0 : -1;
}
