#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <stdlib.h>

#include "dino_portability.h"

// Changing the environment: setenv() and friends are POSIX, _putenv() is
// Windows', and neither side's calls exist on the other. getenv() reads it
// the same everywhere and stays quiet.
#if defined(DINO__WARNINGS) && !defined(DINO__STDLIB_H) && (defined(_STDLIB_H) || defined(_INC_STDLIB))
#define DINO__STDLIB_H

#ifdef _WIN32
_CRTIMP __typeof__(_putenv) _putenv __attribute__((deprecated(
    "_putenv() is Windows-only; use dino_env_set() or dino_env_unset() from <dino/core/process.h>")));
// Strict modes leave each of these out: when unsure, no warning.
#else
#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) \
    || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(setenv) setenv __attribute__((deprecated(
    "setenv() is POSIX-only; use dino_env_set() from <dino/core/process.h>")));
__typeof__(unsetenv) unsetenv __attribute__((deprecated(
    "unsetenv() is POSIX-only; use dino_env_unset() from <dino/core/process.h>")));
#endif
#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_DEFAULT_SOURCE)
__typeof__(putenv) putenv __attribute__((deprecated(
    "putenv() keeps the caller's string and is _putenv() on Windows; use dino_env_set() from <dino/core/process.h>")));
#endif
#ifdef _GNU_SOURCE
__typeof__(clearenv) clearenv __attribute__((deprecated(
    "clearenv() is Linux-only; pass a complete environment to dino_process_spawn() instead")));
#endif
#endif

#endif
