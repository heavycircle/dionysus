#ifndef DINO_CORE_PIPE_INTERNAL_H
#define DINO_CORE_PIPE_INTERNAL_H

#include <stdint.h>

#include <dino/core/pipe.h>

// Internal to the SDK: the OS handle behind a pipe end, for process_*.c to
// hand to a child. A HANDLE on Windows, a file descriptor on POSIX.
intptr_t dino__pipe_to_os(dino_pipe_t pipe);

// A pipe whose child_end goes to a child process's stdio. On Windows the
// child's end is opened for ordinary blocking I/O, which is what a child
// expects of its standard handles; parent_end stays overlapped like the ends
// from dino_pipe_create.
int dino__pipe_create_for_child(dino_pipe_t *parent_end, dino_pipe_t *child_end, int parent_reads);

// 1 for a read end, 0 for a write end, -1 on error.
int dino__pipe_is_read_end(dino_pipe_t pipe);

// Attach a pipe end to an event loop's completion port (a HANDLE). On
// Windows the attachment is permanent: 0 if this is the port it is (now)
// attached to, -1 for any other. Always 0 on POSIX, where there is no port.
int dino__pipe_attach(dino_pipe_t pipe, intptr_t port);

#endif // DINO_CORE_PIPE_INTERNAL_H
