// clang-format off: one redeclaration per call, laid out to be read.
#pragma GCC system_header
#include_next <process.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__PROCESS_H)
#define DINO__PROCESS_H

_CRTIMP __typeof__(_beginthread) _beginthread __attribute__((deprecated(
    "_beginthread() is Windows-only; use dino_thread_create() from <dino/core/thread.h>")));
_CRTIMP __typeof__(_beginthreadex) _beginthreadex __attribute__((deprecated(
    "_beginthreadex() is Windows-only; use dino_thread_create() from <dino/core/thread.h>")));

#endif
