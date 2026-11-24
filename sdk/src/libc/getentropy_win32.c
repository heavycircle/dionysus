#include <stddef.h>

#include "libc_win32_internal.h"

// clang-format off: <ntsecapi.h> needs <windows.h> first.
#include <ntsecapi.h>
// clang-format on

// MinGW has no getentropy. RtlGenRandom comes from advapi32, which the SDK
// already links for its pipe names. Like musl's: at most 256 bytes, EIO beyond that.
int getentropy(void *buffer, size_t length)
{
    if (length > 256)
    {
        errno = EIO;
        return -1;
    }
    if (length && !buffer)
    {
        errno = EFAULT;
        return -1;
    }
    if (length && !RtlGenRandom(buffer, (ULONG)length))
    {
        errno = EIO;
        return -1;
    }
    return 0;
}
