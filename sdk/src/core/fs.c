#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <dino/core/fs.h>

// Base file descriptor for dino_fs_init / dino_fs_cleanup.
int dino_base_fd = -1;

#ifdef _WIN32

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

#else

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

uint64_t dino_file_size(const char *filepath)
{
    struct stat st;

    if (stat(filepath, &st) != 0)
    {
        return 0;
    }

    return (uint64_t)st.st_size;
}

int dino_fs_init(void)
{
    dino_base_fd = open("/dev/null", O_RDWR);
    if (dino_base_fd == 0)
    {
        dup2(0, 1);
        dup2(0, 2);
    }
    else
    {
        close(dino_base_fd);
        dino_base_fd = -1;
    }

    return 0;
}

void dino_fs_cleanup(void)
{
    if (dino_base_fd == 0)
    {
        close(dino_base_fd);
    }
}
#endif
