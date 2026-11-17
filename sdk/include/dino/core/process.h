#ifndef DINO_CORE_PROCESS_H
#define DINO_CORE_PROCESS_H

#include <stdint.h>

#include <dino/core/pipe.h>

typedef uintptr_t dino_process_t;

#define DINO_INVALID_PROCESS ((dino_process_t)0)

/** Exit code of a process ended by dino_process_kill, on every platform. */
#define DINO_EXIT_KILLED 137

/** What a child's stdin, stdout or stderr is connected to. */
typedef enum
{
    DINO_STDIO_INHERIT = 0, /**< The parent's own stream (the default). */
    DINO_STDIO_NULL,        /**< /dev/null or NUL: reads see end of file, writes vanish. */
    DINO_STDIO_PIPE,        /**< A new pipe; the parent's end is in dino_process_pipes_t. */
} dino_stdio_t;

/** Options for dino_process_spawn; zero-initialise and set what you need. */
typedef struct
{
    /** Working directory of the child; NULL for the parent's. */
    const char *cwd;
    /**
     * The child's whole environment as "NAME=value" strings, ending with NULL;
     * NULL for a copy of the parent's. On Windows many programs need SYSTEMROOT.
     */
    char *const *env;
    dino_stdio_t stdin_mode, stdout_mode, stderr_mode;
} dino_spawn_opts_t;

/**
 * The parent's ends of the pipes asked for with DINO_STDIO_PIPE; the others
 * are DINO_INVALID_PIPE. Close them with dino_pipe_close. Closing in makes the
 * child see end of file on stdin.
 */
typedef struct
{
    dino_pipe_t in;  /**< Write here to feed the child's stdin. */
    dino_pipe_t out; /**< Read the child's stdout here. */
    dino_pipe_t err; /**< Read the child's stderr here. */
} dino_process_pipes_t;

/**
 * Start a program.
 *
 * argv[0] is the program: a path, or a bare name looked up on PATH (Windows
 * also looks in the parent's directory, the current directory and the system
 * directories first, and adds .exe). The arguments reach the child exactly as
 * given: on Windows they are quoted by the MSVCRT rules that the child's C
 * runtime uses to split them again. All strings are UTF-8.
 *
 * The child inherits only its stdin, stdout and stderr, never other handles or
 * sockets of the parent.
 *
 * Reading only stdout while the child also fills a stderr pipe can deadlock
 * once that pipe's 64 KB are full; read both, or redirect one to
 * DINO_STDIO_NULL or DINO_STDIO_INHERIT.
 *
 * @param  process  Receives the handle, or DINO_INVALID_PROCESS on failure.
 * @param  argv     Program and arguments, ending with NULL.
 * @param  opts     Options, or NULL for the defaults.
 * @param  pipes    Receives the parent's pipe ends; may be NULL if no stream
 *                  is DINO_STDIO_PIPE.
 * @return 0 on success, -1 if the program couldn't be started (not found, not
 *         executable, bad arguments).
 */
int dino_process_spawn(dino_process_t *process, char *const argv[], const dino_spawn_opts_t *opts,
    dino_process_pipes_t *pipes);

/**
 * Wait for a process to end and release it. The handle is invalid afterwards.
 *
 * Every spawned process must be waited for exactly once; on POSIX it stays a
 * zombie until then.
 *
 * @param  process    Handle from dino_process_spawn.
 * @param  exit_code  Receives how it ended: its exit code; 128 + the signal
 *                    number if a signal ended it (POSIX); DINO_EXIT_KILLED
 *                    after dino_process_kill. May be NULL.
 * @return 0 on success, -1 on error.
 */
int dino_process_wait(dino_process_t process, int *exit_code);

/**
 * End a process immediately (SIGKILL / TerminateProcess). It can't clean up.
 * Still wait for it afterwards.
 *
 * @param  process  Handle from dino_process_spawn, not yet waited for.
 * @return 0 on success, -1 on error.
 */
int dino_process_kill(dino_process_t process);

/**
 * Get the OS process ID, for logs or OS tools.
 *
 * @param  process  Handle from dino_process_spawn.
 * @return The process ID, or 0 for DINO_INVALID_PROCESS.
 */
uint64_t dino_process_id(dino_process_t process);

#endif // DINO_CORE_PROCESS_H
