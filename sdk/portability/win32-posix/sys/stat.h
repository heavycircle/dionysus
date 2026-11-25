// POSIX mkdir(path, mode) on Windows, where MinGW's mkdir takes the path alone
// (Windows has no permission bits), so mkdir(path, 0755) wouldn't compile.
// The SDK defines it (sdk/src/libc/mkdir_win32.c): UTF-8 paths, errno as on
// Linux, and mode ignored. A macro, since C can't declare a second mkdir:
// taking mkdir's address still gets MinGW's one-argument version.
#pragma GCC system_header
#include_next <sys/stat.h>

#if !defined(DINO_NO_LIBC_SHIMS) && !defined(DINO_WIN32_SYS_STAT_MKDIR)
#define DINO_WIN32_SYS_STAT_MKDIR
// MinGW's own one-argument declarations come first, so a later include of
// either header doesn't run into the macro.
#include <direct.h>
#include <io.h>

#ifdef __cplusplus
extern "C" int dino__mkdir(const char *path, mode_t mode);
#else
int dino__mkdir(const char *path, mode_t mode);
#endif
#define mkdir(path, mode) dino__mkdir(path, mode)
#endif
