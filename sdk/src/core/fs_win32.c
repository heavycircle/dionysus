#include <dino/core/fs.h>

#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "error_internal.h"

// A UTF-8 path as the wide string the W functions take; NULL, with the error
// set, if it isn't valid UTF-8 or memory runs out. Free it.
static WCHAR *to_wide(const char *path)
{
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (len <= 0)
    {
        dino__error_set(DINO_EINVAL);
        return NULL;
    }
    WCHAR *wide = malloc((size_t)len * sizeof(WCHAR));
    if (!wide)
    {
        dino__error_set(DINO_ENOMEM);
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);
    return wide;
}

// Opening a named pipe connects to it as a client, taking an instance from
// whoever serves it, so a pipe path is refused without being opened.
static int is_pipe_path(const char *path)
{
    return (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/')
        && (path[2] == '.' || path[2] == '?') && (path[3] == '\\' || path[3] == '/')
        && _strnicmp(path + 4, "pipe", 4) == 0 && (path[8] == '\\' || path[8] == '/');
}

int dino_file_size(const char *path, uint64_t *size)
{
    if (size)
        *size = 0;
    if (!path || !size)
        return dino__fail(DINO_EINVAL);
    if (is_pipe_path(path))
        return dino__fail(DINO_EINVAL);

    WCHAR *wide = to_wide(path);
    if (!wide)
        return -1;

    // Opening the path follows symbolic links, as stat does on Linux;
    // GetFileAttributesExW would describe the link itself. Backup semantics
    // let a directory open too, so it can be told apart and rejected. Only
    // FILE_READ_ATTRIBUTES and full sharing are asked for, so a file another
    // process has open without sharing still has a size.
    HANDLE file = CreateFileW(wide, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        dino__error_set_os();
        free(wide);
        return -1;
    }
    free(wide);
    // A device (NUL, CON, a serial port) is not a regular file, as on Linux.
    if (GetFileType(file) != FILE_TYPE_DISK)
    {
        CloseHandle(file);
        return dino__fail(DINO_EINVAL);
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info))
    {
        dino__error_set_os();
        CloseHandle(file);
        return -1;
    }
    CloseHandle(file);
    // A directory: there, but with no size to report.
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return dino__fail(DINO_EINVAL);
    *size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return 0;
}

// The missing-stdio problem only affects processes spawned by the Linux kernel's
// call_usermodehelper, so there is nothing to set up on Windows.
int dino_fs_init(void)
{
    return 0;
}

// As snprintf: as much as fits, NUL-terminated, and the full length back, for
// a wide string converted to UTF-8.
static int copy_out_wide(char *buf, size_t size, const WCHAR *text, int wlen)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, text, wlen, NULL, 0, NULL, NULL);
    if (len <= 0 && wlen > 0)
        return dino__fail(DINO_EINVAL);
    char *utf8 = malloc((size_t)len + 1);
    if (!utf8)
        return dino__fail(DINO_ENOMEM);
    WideCharToMultiByte(CP_UTF8, 0, text, wlen, utf8, len, NULL, NULL);
    if (size > 0)
    {
        size_t n = (size_t)len < size - 1 ? (size_t)len : size - 1;
        memcpy(buf, utf8, n);
        buf[n] = '\0';
    }
    free(utf8);
    return len;
}

int dino_path_temp(char *buf, size_t size)
{
    if (!buf && size)
        return dino__fail(DINO_EINVAL);
    WCHAR path[MAX_PATH + 2];
    DWORD len = GetTempPathW(MAX_PATH + 2, path);
    if (len == 0 || len > MAX_PATH + 1)
        return dino__fail_os();
    // Without the trailing backslash GetTempPathW always adds, unless all that
    // is left is a drive's root ("C:\").
    while (len > 3 && (path[len - 1] == L'\\' || path[len - 1] == L'/'))
        len--;
    return copy_out_wide(buf, size, path, (int)len);
}

int dino_path_executable(char *buf, size_t size)
{
    if (!buf && size)
        return dino__fail(DINO_EINVAL);
    // GetModuleFileNameW cuts a long path short and says so only by filling
    // the buffer: grow until it fits, up to the 32767 characters a path has.
    for (DWORD cap = MAX_PATH; cap <= 32768; cap *= 2)
    {
        WCHAR *path = malloc(cap * sizeof(WCHAR));
        if (!path)
            return dino__fail(DINO_ENOMEM);
        DWORD len = GetModuleFileNameW(NULL, path, cap);
        if (len == 0)
        {
            dino__error_set_os();
            free(path);
            return -1;
        }
        if (len < cap)
        {
            int rc = copy_out_wide(buf, size, path, (int)len);
            free(path);
            return rc;
        }
        free(path);
    }
    return dino__fail(DINO_EINVAL);
}
