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
#define IS_SEPARATOR(c)    ((c) == '/' || (c) == '\\')
#define OWNER_WRITE_SEARCH 0 // no permission bits

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

// Past the next n components of path (len characters), from i: "server\share\".
static size_t skip_components(const char *path, size_t len, size_t i, int n)
{
    for (; n > 0 && i < len; n--)
    {
        while (i < len && !IS_SEPARATOR(path[i]))
            i++;
        if (i < len)
            i++;
    }
    return i;
}

// The part of path (len characters) that is a root, never created: "C:\",
// "\\server\share\", "\\?\C:\", "\\?\UNC\server\share\", "\\?\Volume{...}\",
// or a leading separator.
static size_t root_length(const char *path, size_t len)
{
    if (len >= 2 && IS_SEPARATOR(path[0]) && IS_SEPARATOR(path[1]))
    {
        if (len >= 4 && (path[2] == '?' || path[2] == '.') && IS_SEPARATOR(path[3]))
        {
            if (len >= 6 && path[5] == ':')
                return len > 6 ? 7 : 6;
            if (len >= 8 && _strnicmp(path + 4, "UNC", 3) == 0 && IS_SEPARATOR(path[7]))
                return skip_components(path, len, 8, 2);
            return skip_components(path, len, 4, 1); // Volume{GUID}, GLOBALROOT, a device
        }
        return skip_components(path, len, 2, 2);
    }
    if (len >= 2 && path[1] == ':')
        return len >= 3 && IS_SEPARATOR(path[2]) ? 3 : 2;
    return len >= 1 && IS_SEPARATOR(path[0]) ? 1 : 0;
}
#else
#define IS_SEPARATOR(c)    ((c) == '/')
#define OWNER_WRITE_SEARCH (S_IWUSR | S_IXUSR)

static int make_one(const char *path, mode_t mode)
{
    return mkdir(path, mode) == 0 ? 0 : dino__fail_os();
}

static int is_directory(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static size_t root_length(const char *path, size_t len)
{
    return len >= 1 && path[0] == '/' ? 1 : 0;
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

    // The end of the last component: only that directory gets mode as given.
    size_t last = len;
    while (last > 0 && IS_SEPARATOR(copy[last - 1]))
        last--;

    // A part that already exists fails mkdir; a call that succeeds leaves
    // dino_error() as it found it, as every SDK call does.
    int before = dino_error();
    int rc     = 0;
    for (size_t i = root_length(copy, len); rc == 0 && i <= len; i++)
    {
        if (i < len && !IS_SEPARATOR(copy[i]))
            continue;
        // A component ends here: a separator, or the end of the path.
        if (i == 0 || IS_SEPARATOR(copy[i - 1]))
            continue; // "a//b", or a trailing separator
        char saved = copy[i];
        copy[i]    = '\0';
        // As mkdir -p: the directories on the way can always be written and
        // entered by their owner, or the next one couldn't be made in them.
        mode_t level = i >= last ? mode : (mode_t)(mode | OWNER_WRITE_SEARCH);
        // There already is fine if it's a directory; anything else in the way
        // fails with mkdir's reason (DINO_EEXIST for a file).
        if (make_one(copy, level) != 0 && !is_directory(copy))
            rc = -1;
        copy[i] = saved;
    }
    free(copy);
    if (rc == 0)
        dino__error_set(before);
    return rc;
}
