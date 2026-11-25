#ifndef DINO_CORE_PIPE_H
#define DINO_CORE_PIPE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct dino_pipe *dino_pipe_t;

#define DINO_INVALID_PIPE ((dino_pipe_t)0)

#ifndef DINO_TIMEOUT
/** Returned by dino_pipe_wait when the time ran out (as in dino/core/thread.h). */
#define DINO_TIMEOUT 1
#endif

/**
 * Create a pipe: bytes written to write_end come out of read_end.
 *
 * Both ends normally hold 64 KB before a write blocks (on Linux a user over
 * the kernel's pipe-buffer limit gets 4 KB), and neither is inherited by child
 * processes. (A child's stdio pipes come from dino_process_spawn.)
 *
 * Pipes are not sockets: poll and select don't accept them on Windows. Wait on
 * one read end with dino_pipe_wait, or on many alongside sockets with
 * dino_event_add_pipe (dino/core/event.h).
 *
 * @param  read_end   Receives the reading end, or DINO_INVALID_PIPE on failure.
 * @param  write_end  Receives the writing end, or DINO_INVALID_PIPE on failure.
 * @return 0 on success, -1 on failure.
 */
int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end);

/** dino_pipe_create_ex: the read end goes to a child process's stdin. */
#define DINO_PIPE_CHILD_READS 1
/** dino_pipe_create_ex: the write end goes to a child's stdout or stderr. */
#define DINO_PIPE_CHILD_WRITES 2

/**
 * Create a pipe with an end, or both, meant for child processes' stdio, to
 * pass with DINO_STDIO_PIPE_END (dino/core/process.h): one child's output into
 * another's input (a | b, both flags), or a pipe whose other end the parent
 * keeps. Since 1.0.
 *
 * A child's C runtime does blocking I/O on its standard handles, which Windows
 * doesn't allow on the overlapped handles dino_pipe_create makes; a marked end
 * is an ordinary blocking one. So, on every platform alike, a marked end can't
 * be waited for with dino_pipe_wait or added to an event loop; read and write
 * work. dino_process_spawn refuses an end that isn't marked.
 *
 * @param  read_end   Receives the reading end, or DINO_INVALID_PIPE on failure.
 * @param  write_end  Receives the writing end, or DINO_INVALID_PIPE on failure.
 * @param  flags      DINO_PIPE_CHILD_READS and/or DINO_PIPE_CHILD_WRITES; 0 is
 *                    dino_pipe_create.
 * @return 0 on success, -1 on failure.
 */
int dino_pipe_create_ex(dino_pipe_t *read_end, dino_pipe_t *write_end, int flags);

/**
 * Read up to len bytes, blocking until at least one is available.
 *
 * @param  pipe  Reading end.
 * @param  buf   Destination.
 * @param  len   Maximum number of bytes to read. One call reads at most
 *               INT_MAX bytes.
 * @return Bytes read, 0 at end of file (every write end is closed) or when len
 *         is 0, -1 on error.
 */
int dino_pipe_read(dino_pipe_t pipe, void *buf, size_t len);

/**
 * Write up to len bytes, blocking while the pipe is full.
 *
 * May write fewer bytes than asked (for example after a signal on POSIX); write
 * the rest with another call. Writing after every read end has closed returns
 * -1 and never raises SIGPIPE: the process survives, and its signal mask and
 * SIGPIPE handling are left as they were.
 *
 * @param  pipe  Writing end.
 * @param  buf   Source.
 * @param  len   Number of bytes to write. One call writes at most INT_MAX
 *               bytes; write the rest with another call.
 * @return Bytes written, -1 on error; dino_error() (dino/core/error.h) is
 *         DINO_EPIPE when every read end is closed.
 */
int dino_pipe_write(dino_pipe_t pipe, const void *buf, size_t len);

/**
 * Wait until a read won't block: data is available or every write end has
 * closed (the read then returns 0).
 *
 * @param  pipe        Reading end.
 * @param  timeout_ms  Milliseconds to wait; -1 to wait forever; 0 to check.
 * @return 0 when readable, DINO_TIMEOUT when the time ran out, -1 on error,
 *         including for a write end, or an end marked for a child
 *         (dino_pipe_create_ex).
 */
int dino_pipe_wait(dino_pipe_t pipe, int timeout_ms);

/**
 * Close one end of a pipe.
 *
 * Closing every write end makes reads return 0 once the data is drained.
 *
 * @param  pipe  Either end.
 * @return 0 on success, -1 on error or for DINO_INVALID_PIPE.
 */
int dino_pipe_close(dino_pipe_t pipe);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_PIPE_H
