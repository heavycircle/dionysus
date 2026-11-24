#ifndef DINO_CORE_ERROR_H
#define DINO_CORE_ERROR_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Why the last SDK call in this thread failed.
 *
 * Functions report failure through their return value: -1, or the
 * DINO_INVALID_* value of a handle. The reason is here, one per thread, the
 * same codes on every platform:
 *
 *     int n = dino_socket_recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
 *     if (n < 0 && dino_error() == DINO_EAGAIN)
 *         ... nothing to read yet ...
 *
 * Only a failed call sets it; a call that succeeds, or returns DINO_TIMEOUT,
 * leaves the previous value. Read it right after the failure, before any other
 * SDK call in the thread.
 */

/** A failure with no code of its own below. */
#define DINO_EOTHER 1
/** An argument is invalid: a null pointer, an invalid handle, a bad value. */
#define DINO_EINVAL 2
/** Not now: a non-blocking call would have had to wait. Try again later. */
#define DINO_EAGAIN 3
/** A non-blocking connect has started and is not finished (dino_socket_error). */
#define DINO_EINPROGRESS 4
/** Out of memory. */
#define DINO_ENOMEM 5
/** No such file, directory or program. */
#define DINO_ENOENT 6
/** Permission denied. */
#define DINO_EACCES 7
/** It already exists. */
#define DINO_EEXIST 8
/** Too many open files, sockets or handles, in the process or the system. */
#define DINO_EMFILE 9
/** Nothing is listening at the address: the connection was refused. */
#define DINO_ECONNREFUSED 10
/** The peer reset or aborted the connection. */
#define DINO_ECONNRESET 11
/** The other end is closed: nothing can be written any more. */
#define DINO_EPIPE 12
/** The address is already in use. */
#define DINO_EADDRINUSE 13
/** The operation itself timed out, such as a connection attempt. */
#define DINO_ETIMEDOUT 14
/** The network or the host can't be reached. */
#define DINO_EUNREACH 15
/** Not connected. */
#define DINO_ENOTCONN 16
/** This platform, or this kernel, can't do it. */
#define DINO_ENOTSUP 17

/**
 * The reason for the last failed SDK call in the calling thread.
 *
 * @return One of the DINO_E* codes, or 0 if no call has failed in this thread.
 */
int dino_error(void);

/**
 * A short English description of a code, for logs and error messages.
 *
 * @param  code  A DINO_E* code, typically from dino_error().
 * @return A constant string; never NULL, also for a value that is no code.
 */
const char *dino_error_message(int code);

#ifdef __cplusplus
}
#endif

#endif /* DINO_CORE_ERROR_H */
