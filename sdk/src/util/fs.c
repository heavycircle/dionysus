#include <dino/util/fs.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../core/error_internal.h"

#ifdef _WIN32
#define IS_SEPARATOR(c) ((c) == '/' || (c) == '\\')

// One level. Not through the mkdir shim: it isn't part of the SDK's own
// build, and a project may leave the shims out.
static int make_one(const char *path, mode_t mode)
{
    (void)mode;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (len <= 0)
        return dino__fail(DINO_EINVAL);
    WCHAR *wide = malloc((size_t)len * sizeof(WCHAR));
    if (!wide)
        return dino__fail(DINO_ENOMEM);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);
    int ok = CreateDirectoryW(wide, NULL) != 0;
    if (!ok)
        dino__error_set_os();
    free(wide);
    return ok ? 0 : -1;
}

static int is_directory(const char *path)
{
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (len <= 0)
        return 0;
    WCHAR *wide = malloc((size_t)len * sizeof(WCHAR));
    if (!wide)
        return 0;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, len);
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The part of path that is a root, never created: "C:\", "\\server\share\",
// "\\?\C:\", or a leading separator.
static size_t root_length(const char *path)
{
    size_t i = 0;
    if (IS_SEPARATOR(path[0]) && IS_SEPARATOR(path[1]))
    {
        i = 2;
        if ((path[2] == '?' || path[2] == '.') && IS_SEPARATOR(path[3]))
        {
            i = 4;
            if (path[i] && path[i + 1] == ':')
                return path[i + 2] ? i + 3 : i + 2;
        }
        // \\server\share\: two components.
        for (int parts = 0; parts < 2 && path[i]; parts++)
        {
            while (path[i] && !IS_SEPARATOR(path[i]))
                i++;
            if (path[i])
                i++;
        }
        return i;
    }
    if (path[0] && path[1] == ':')
        return IS_SEPARATOR(path[2]) ? 3 : 2;
    return IS_SEPARATOR(path[0]) ? 1 : 0;
}
#else
#define IS_SEPARATOR(c) ((c) == '/')

static int make_one(const char *path, mode_t mode)
{
    return mkdir(path, mode) == 0 ? 0 : dino__fail_os();
}

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static size_t root_length(const char *path)
{
    return path[0] == '/' ? 1 : 0;
}
#endif

int dino_mkdir_p(const char *path, mode_t mode)
{
    if (!path || !*path)
        return dino__fail(DINO_EINVAL);
    size_t len  = strlen(path);
    char  *copy = malloc(len + 1);
    if (!copy)
        return dino__fail(DINO_ENOMEM);
    memcpy(copy, path, len + 1);

    int rc = 0;
    for (size_t i = root_length(copy); rc == 0 && i <= len; i++)
    {
        if (i < len && !IS_SEPARATOR(copy[i]))
            continue;
        // A component ends here: a separator, or the end of the path.
        if (i == 0 || IS_SEPARATOR(copy[i - 1]))
            continue; // "a//b", or a trailing separator
        char saved = copy[i];
        copy[i]    = '\0';
        // There already is fine if it's a directory; anything else in the way
        // fails with mkdir's reason (DINO_EEXIST for a file).
        if (make_one(copy, mode) != 0 && !is_directory(copy))
            rc = -1;
        copy[i] = saved;
    }
    free(copy);
    return rc;
}
