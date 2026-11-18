// clang-format off: one redeclaration per call, laid out to be read.
#pragma GCC system_header
#include_next <sys/select.h>

#include "dino_portability.h"

#if defined(DINO__WARNINGS) && !defined(DINO__SYS_SELECT_H)
#define DINO__SYS_SELECT_H

__typeof__(select) select __attribute__((deprecated(
    "select() takes SOCKET sets on Windows; use dino_select() from <dino/core/io.h>")));

#endif
