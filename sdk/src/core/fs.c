#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <dino/core/fs.h>

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

// The missing-stdio problem only affects processes spawned by the Linux kernel's
// call_usermodehelper, so there is nothing to set up on Windows.
int dino_fs_init(void)
{
    return 0;
}

void dino_fs_cleanup(void)
{
}

#else

#include <errno.h>
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
    // Only fill descriptors that are missing: stdio the program was started
    // with, even just stdout or stderr, must survive.
    for (int fd = 0; fd <= 2; fd++)
    {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            continue;

        int null = open("/dev/null", O_RDWR);
        if (null < 0)
            return -1;
        if (null != fd)
        {
            dup2(null, fd);
            close(null);
        }
    }

    return 0;
}

// Stdio belongs to the program, so there is nothing to undo.
void dino_fs_cleanup(void)
{
}
#endif
