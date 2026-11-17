#ifndef DINO_CORE_PIPE_INTERNAL_H
#define DINO_CORE_PIPE_INTERNAL_H

#include <stdint.h>

#include <dino/core/pipe.h>

// Internal to the SDK: the OS handle behind a pipe end, for process_*.c to
// hand to a child. A HANDLE on Windows, a file descriptor on POSIX.
intptr_t dino__pipe_to_os(dino_pipe_t pipe);

#endif // DINO_CORE_PIPE_INTERNAL_H
