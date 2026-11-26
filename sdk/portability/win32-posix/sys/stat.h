#pragma GCC system_header
// POSIX mkdir(path, mode) on Windows, where MinGW's mkdir takes the path alone
// (Windows has no permission bits), so mkdir(path, 0755) wouldn't compile.
// The SDK defines it (sdk/src/libc/mkdir_win32.c): UTF-8 paths, errno as on
// Linux, and mode ignored. A macro, since C can't declare a second mkdir. It
// takes the path alone too, as MinGW's does, so Windows-only mkdir(path) still
// builds. Left out when mkdir is already a macro (a project's own), and with
// DINO_NO_MKDIR_SHIM or DINO_NO_LIBC_SHIMS. Taking mkdir's address still gets
// MinGW's version, and a member called mkdir (ops->mkdir(p, m)) needs
// parentheses: (ops->mkdir)(p, m).
#include_next <sys/stat.h>

#if !defined(DINO_NO_LIBC_SHIMS) && !defined(DINO_NO_MKDIR_SHIM) && !defined(mkdir)                \
    && !defined(DINO_WIN32_SYS_STAT_MKDIR)
#define DINO_WIN32_SYS_STAT_MKDIR
// MinGW's own one-argument declarations come first, so a later include of
// either header doesn't run into the macro.
#include <direct.h>
#include <io.h>

// int, not mode_t: MinGW leaves mode_t undefined under NO_OLDNAMES.
#ifdef __cplusplus
extern "C"
{
#endif
int dino__mkdir(const char *path, int mode);
int dino__mkdir_path(const char *path);
#ifdef __cplusplus
}
#endif
// mkdir(path, mode) or mkdir(path): the argument count picks the function.
#define DINO__MKDIR_PICK(path, mode, name, ...) name
#define mkdir(...)                              DINO__MKDIR_PICK(__VA_ARGS__, dino__mkdir, dino__mkdir_path, 0)(__VA_ARGS__)
#endif
