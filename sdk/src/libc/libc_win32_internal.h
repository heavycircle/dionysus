#ifndef DINO_LIBC_WIN32_INTERNAL_H
#define DINO_LIBC_WIN32_INTERNAL_H

#include <errno.h>
#include <stdlib.h>

#include <windows.h>

// Internal to the SDK: what the Windows definitions of C library calls in
// this directory share. Each call is its own file, so a program only links the
// ones it calls, and a project's own definition of one wins over the archive.

// A UTF-8 string as the wide string the W functions take; NULL with errno set
// (EINVAL for invalid UTF-8, ENOMEM) otherwise. Free it.
static inline wchar_t *dino__libc_widen(const char *s)
{
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (len <= 0)
    {
        errno = EINVAL;
        return NULL;
    }
    wchar_t *wide = malloc((size_t)len * sizeof(wchar_t));
    if (!wide)
    {
        errno = ENOMEM;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, wide, len);
    return wide;
}

// errno for the last Win32 failure of a file call, as Linux would set it.
static inline int dino__libc_errno(void)
{
    switch (GetLastError())
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_PRIVILEGE_NOT_HELD:
        return EACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        return EEXIST;
    case ERROR_DIR_NOT_EMPTY:
        return ENOTEMPTY;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_DIRECTORY:
        return ENOTDIR;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_WRITE_PROTECT:
        return EROFS;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    case ERROR_INVALID_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_INVALID_PARAMETER:
        return EINVAL;
    default:
        return EIO;
    }
}

#endif // DINO_LIBC_WIN32_INTERNAL_H
