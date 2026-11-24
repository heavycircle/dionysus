#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <stdlib.h>

#include "dino_portability.h"

// MinGW has no setenv or unsetenv; the SDK defines them
// (sdk/src/libc/setenv_win32.c).
#if defined(_WIN32) && defined(_INC_STDLIB) && !defined(DINO_NO_LIBC_SHIMS) && !defined(DINO__STDLIB_SHIMS)
#define DINO__STDLIB_SHIMS
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
#endif

// Changing the environment: setenv() and unsetenv() work on every target (the
// SDK provides them on Windows); the platforms' other ways don't.
#if defined(DINO__WARNINGS) && !defined(DINO__STDLIB_H) && (defined(_STDLIB_H) || defined(_INC_STDLIB))
#define DINO__STDLIB_H

#ifdef _WIN32
_CRTIMP __typeof__(_putenv) _putenv __attribute__((deprecated(
    "_putenv() is Windows-only and treats an empty value as unset; use setenv() or unsetenv() from <stdlib.h>, which work on every target")));
#else
#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(putenv) putenv __attribute__((deprecated(
    "putenv() keeps the caller's string and is _putenv() on Windows; use setenv() from <stdlib.h>, which works on every target")));
#endif
#ifdef _GNU_SOURCE
__typeof__(clearenv) clearenv __attribute__((deprecated(
    "clearenv() is Linux-only; pass a complete environment to dino_process_spawn() instead")));
#endif
#endif

#endif
