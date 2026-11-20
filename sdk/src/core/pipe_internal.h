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

// dino_pipe_write that gives up after timeout_ms (-1: never): 0 with the bytes
// sent in *written (possibly fewer than len), DINO_TIMEOUT with nothing sent,
// -1 on error. Lets dino_process_run stop feeding a child at its deadline.
int dino__pipe_write_timeout(
    dino_pipe_t pipe, const void *buf, int len, int timeout_ms, int *written);

// 1 for a read end, 0 for a write end, -1 on error.
int dino__pipe_is_read_end(dino_pipe_t pipe);

// Attach a pipe end to an event loop's completion port (a HANDLE). On
// Windows the attachment is permanent: 0 if this is the loop it is (now)
// attached to, -1 for any other. Loops are told apart by loop_id, unique for
// the process, because a destroyed loop's port HANDLE value gets reused.
// Always 0 on POSIX, where there is no port.
int dino__pipe_attach(dino_pipe_t pipe, intptr_t port, uint64_t loop_id);

#endif // DINO_CORE_PIPE_INTERNAL_H
