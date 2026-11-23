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

// No MOVEFILE_COPY_ALLOWED: across volumes it copies, which isn't a rename and
// isn't atomic. Linux fails there too (EXDEV).
int dino_file_rename(const char *from, const char *to)
{
    if (!from || !to)
        return dino__fail(DINO_EINVAL);
    WCHAR *wfrom = to_wide(from);
    if (!wfrom)
        return -1;
    WCHAR *wto = to_wide(to);
    if (!wto)
    {
        free(wfrom);
        return -1;
    }
    BOOL ok = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        dino__error_set_os();
    free(wto);
    free(wfrom);
    return ok ? 0 : -1;
}

int dino_file_remove(const char *path)
{
    if (!path)
        return dino__fail(DINO_EINVAL);
    WCHAR *wide = to_wide(path);
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
        dino__error_set_os();
    free(wide);
    return ok ? 0 : -1;
}

// The missing-stdio problem only affects processes spawned by the Linux kernel's
// call_usermodehelper, so there is nothing to set up on Windows.
int dino_fs_init(void)
{
    return 0;
}
