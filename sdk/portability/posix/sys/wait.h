#pragma GCC system_header
// clang-format off: one redeclaration per call, laid out to be read.
#include_next <sys/wait.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_WAIT_H) && (defined(_SYS_WAIT_H))
#define DINO__SYS_WAIT_H

__typeof__(waitpid) waitpid __attribute__((deprecated(
    "waitpid() is POSIX-only; use dino_process_wait() from <dino/core/process.h>")));

#endif
