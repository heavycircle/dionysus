#include <dino/core/fs.h>

#include <stdlib.h>

#include <windows.h>

// The attributes come without opening the file, so a file another process has
// open without sharing still has a size.
int dino_file_size(const char *path, uint64_t *size)
{
    if (size)
        *size = 0;
    if (!path || !size)
        return -1;

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (len <= 0)
        return -1;
    WCHAR *wide = malloc((size_t)len * sizeof(WCHAR));
    if (!wide)
        return -1;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);

    WIN32_FILE_ATTRIBUTE_DATA info;
    BOOL                      ok = GetFileAttributesExW(wide, GetFileExInfoStandard, &info);
    free(wide);
    if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    *size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    return 0;
}

// The missing-stdio problem only affects processes spawned by the Linux kernel's
// call_usermodehelper, so there is nothing to set up on Windows.
int dino_fs_init(void)
{
    return 0;
}
