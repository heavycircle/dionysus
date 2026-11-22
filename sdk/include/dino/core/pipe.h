#ifndef DINO_CORE_PIPE_H
#define DINO_CORE_PIPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef uintptr_t dino_pipe_t;

#define DINO_INVALID_PIPE ((dino_pipe_t)0)

#ifndef DINO_TIMEOUT
/** Returned by dino_pipe_wait when the time ran out (as in dino/core/mutex.h). */
#define DINO_TIMEOUT 1
#endif

/**
 * Create a pipe: bytes written to write_end come out of read_end.
 *
 * Both ends hold 64 KB before a write blocks, and neither is inherited by child
 * processes unless passed to one explicitly.
 *
 * Pipes are not sockets: dino_poll and dino_select don't accept them. Wait on
 * one read end with dino_pipe_wait, or on many alongside sockets with
 * dino_event_add_pipe (dino/core/event.h).
 *
 * @param  read_end   Receives the reading end, or DINO_INVALID_PIPE on failure.
 * @param  write_end  Receives the writing end, or DINO_INVALID_PIPE on failure.
 * @return 0 on success, -1 on failure.
 */
int dino_pipe_create(dino_pipe_t *read_end, dino_pipe_t *write_end);

/**
 * Read up to len bytes, blocking until at least one is available.
 *
 * @param  pipe  Reading end.
 * @param  buf   Destination.
 * @param  len   Maximum number of bytes to read.
 * @return Bytes read, 0 at end of file (every write end is closed) or when len
 *         is 0, -1 on error.
 */
int dino_pipe_read(dino_pipe_t pipe, void *buf, int len);

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
 * @param  len   Number of bytes to write.
 * @return Bytes written, -1 on error; dino_error() (dino/core/error.h) is
 *         DINO_EPIPE when every read end is closed.
 */
int dino_pipe_write(dino_pipe_t pipe, const void *buf, int len);

/**
 * Wait until a read won't block: data is available or every write end has
 * closed (the read then returns 0).
 *
 * @param  pipe        Reading end.
 * @param  timeout_ms  Milliseconds to wait; -1 to wait forever; 0 to check.
 * @return 0 when readable, DINO_TIMEOUT when the time ran out, -1 on error.
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
