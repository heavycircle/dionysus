#ifndef DINO_CORE_PIPE_HANDLE_H
#define DINO_CORE_PIPE_HANDLE_H

// Internal to the SDK (pipe.c, process.c): dino_pipe_t to and from the OS
// handle. On POSIX it's fd + 1, so fd 0 (possible when stdin was closed) isn't
// DINO_INVALID_PIPE.
#ifdef _WIN32
#define DINO__PIPE_TO_OS(pipe) ((HANDLE)(pipe))
#define DINO__PIPE_FROM_OS(h)  ((dino_pipe_t)(h))
#else
#define DINO__PIPE_TO_OS(pipe) ((int)((pipe) - 1))
#define DINO__PIPE_FROM_OS(fd) ((dino_pipe_t)(fd) + 1)
#endif

#endif // DINO_CORE_PIPE_HANDLE_H
