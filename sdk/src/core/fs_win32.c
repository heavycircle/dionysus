#include <dino/core/fs.h>

#include <stdlib.h>

#include <windows.h>

#include "error_internal.h"

// The attributes come without opening the file, so a file another process has
// open without sharing still has a size.
int dino_file_size(const char *path, uint64_t *size)
{
    if (size)
        *size = 0;
    if (!path || !size)
        return dino__fail(DINO_EINVAL);

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (len <= 0)
        return dino__fail(DINO_EINVAL);
    WCHAR *wide = malloc((size_t)len * sizeof(WCHAR));
    if (!wide)
        return dino__fail(DINO_ENOMEM);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);

    // Opening the path follows symbolic links, as stat does on Linux;
    // GetFileAttributesExW would describe the link itself. Backup semantics
    // let a directory open too, so it can be told apart and rejected.
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
