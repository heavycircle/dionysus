#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <spawn.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SPAWN_H) && (defined(_SPAWN_H))
#define DINO__SPAWN_H

__typeof__(posix_spawn) posix_spawn __attribute__((deprecated(
    "posix_spawn() is POSIX-only; use dino_process_spawn() from <dino/core/process.h>")));
__typeof__(posix_spawnp) posix_spawnp __attribute__((deprecated(
    "posix_spawnp() is POSIX-only; use dino_process_spawn() from <dino/core/process.h>")));

#endif
