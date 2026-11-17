#include <dino/core/fs.h>

#include <windows.h>

uint64_t dino_file_size(const char *filepath)
{
    HANDLE hFile = CreateFileA(
        filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size))
    {
        CloseHandle(hFile);
        return 0; // Error reading size
    }

    CloseHandle(hFile);
    return (uint64_t)size.QuadPart;
}

// The missing-stdio problem only affects processes spawned by the Linux kernel's
// call_usermodehelper, so there is nothing to set up on Windows.
int dino_fs_init(void)
{
    return 0;
}

void dino_fs_cleanup(void)
{
}
