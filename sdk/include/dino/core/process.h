#ifndef DINO_CORE_PROCESS_H
#define DINO_CORE_PROCESS_H

#include <stddef.h>
#include <stdint.h>

#include <dino/core/pipe.h>

#ifdef __cplusplus
extern "C"
{
#endif

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
} dino_process_spawn_opts_t;

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
 * directories first, and adds .exe). A relative path (./tool, bin/tool) is
 * found from opts->cwd when that is set, on Windows too, where .exe is added.
 * The arguments reach the child exactly as given: on Windows they are quoted
 * by the MSVCRT rules that the child's C runtime uses to split them again.
 * A Windows program that reads its command line itself sees that quoting.
 * cmd.exe is one: it doesn't know \", so { "cmd.exe", "/c", command } only
 * works for a command without a " in it. All strings are UTF-8.
 *
 * A Windows .bat or .cmd file runs under cmd.exe, which splits its command
 * line by different rules. Each argument is quoted so that cmd.exe operators
 * (& | < > ^ ...) in it stay literal, and an argument that quoting can't
 * protect, one containing ", % or a line break, is refused with -1. The
 * script still receives each argument with its quotes (%1 is "a b"; %~1
 * removes them).
 *
 * The child inherits only its stdin, stdout and stderr, never other handles or
 * sockets of the parent, even ones opened without the SDK and without
 * close-on-exec. On Linux those are closed in the child by listing
 * /proc/self/fd: one another thread opens during the spawn can still reach it,
 * and without /proc they are inherited.
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
 *         executable, bad arguments, a stdio mode outside dino_stdio_t).
 */
int dino_process_spawn(dino_process_t *process, char *const argv[],
    const dino_process_spawn_opts_t *opts, dino_process_pipes_t *pipes);

/**
 * Wait for a process to end and release it. The handle is invalid afterwards.
 *
 * Every spawned process must be waited for exactly once; on POSIX it stays a
 * zombie until then.
 *
 * On POSIX the exit status goes to whoever asks first. The program must not
 * ignore SIGCHLD (SIG_IGN or SA_NOCLDWAIT), and nothing else in it may wait
 * for the child (wait(), waitpid(-1, ...)): the kernel or that code then takes
 * the status, and every wait here, dino_process_run included, fails with -1
 * although the child ran. The child's process ID is then free to be reused,
 * so don't call dino_process_kill on the handle after that.
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
 * Still wait for it afterwards. A process that already exited is left as it
 * was, keeping its own exit code.
 *
 * Only this one process ends. What it started keeps running: the programs a
 * shell script runs, and for a Windows .bat or .cmd file, where the process
 * is the cmd.exe that runs it, everything the script started.
 *
 * @param  process  Handle from dino_process_spawn, not yet waited for.
 * @return 0 on success (including when it had already exited), -1 on error.
 */
int dino_process_kill(dino_process_t process);

/**
 * Get the OS process ID, for logs or OS tools.
 *
 * @param  process  Handle from dino_process_spawn.
 * @return The process ID, or 0 for DINO_INVALID_PROCESS.
 */
uint64_t dino_process_id(dino_process_t process);

/**
 * Like dino_process_wait, but give up after timeout_ms. On DINO_TIMEOUT the
 * process keeps running and the handle stays valid; on 0 it is released.
 *
 * On Linux 5.3 and later it sleeps until the process exits (on a pidfd); older
 * kernels check every few milliseconds instead.
 *
 * @param  process     Handle from dino_process_spawn.
 * @param  timeout_ms  Milliseconds to wait; -1 to wait forever; 0 to check.
 * @param  exit_code   As for dino_process_wait; may be NULL.
 * @return 0 when it ended, DINO_TIMEOUT when the time ran out, -1 on error.
 */
int dino_process_wait_timeout(dino_process_t process, int timeout_ms, int *exit_code);

/* ---- Run to completion ---- */

/** Options for dino_process_run; zero-initialise and set what you need. */
typedef struct
{
    const char  *cwd; /**< As in dino_process_spawn_opts_t. */
    char *const *env; /**< As in dino_process_spawn_opts_t. */
    /** Bytes for the child's stdin, then end of file. NULL: stdin is null. */
    const void *input;
    size_t      input_len;
    /** Give up after this many milliseconds, killing the child if it is still
     *  running; 0 waits forever. As with dino_process_kill, what the child
     *  started is not killed. */
    int timeout_ms;
} dino_process_run_opts_t;

/** What dino_process_run collected; release with dino_process_result_free. */
typedef struct
{
    char  *out; /**< Everything written to stdout, NUL-terminated. */
    size_t out_len;
    char  *err; /**< Everything written to stderr, NUL-terminated. */
    size_t err_len;
    int    exit_code; /**< As from dino_process_wait; DINO_EXIT_KILLED if killed. */
} dino_process_result_t;

/**
 * Run a program to completion, collecting its stdout and stderr.
 *
 * Both outputs are read, and the input written, at the same time, so a child
 * that fills one pipe while the parent waits on another can't deadlock. It
 * returns once the child has exited and both outputs are closed. A process the
 * child started that keeps the pipes open keeps it waiting, up to the timeout:
 * then it stops reading and writing and returns DINO_TIMEOUT with the child's
 * own exit code.
 *
 * @param  argv    Program and arguments, as for dino_process_spawn.
 * @param  opts    Options, or NULL for the defaults.
 * @param  result  Receives the output and exit code, even on DINO_TIMEOUT
 *                 (what was written before the kill). Free it either way.
 * @return 0 when the child ran to the end, DINO_TIMEOUT when opts->timeout_ms
 *         ran out (the child was killed, or something it started still held
 *         the pipes), -1 if it couldn't be started or on error.
 */
int dino_process_run(
    char *const argv[], const dino_process_run_opts_t *opts, dino_process_result_t *result);

/**
 * Free what dino_process_run collected. Safe on a zeroed result.
 *
 * @param  result  Result to release; its fields are reset.
 */
void dino_process_result_free(dino_process_result_t *result);

/* ---- The current process ---- */

/**
 * Get the calling process's OS process ID.
 *
 * @return getpid() / GetCurrentProcessId().
 */
uint64_t dino_process_current_id(void);

/**
 * Read an environment variable of this process. UTF-8 on every platform.
 *
 * Like snprintf, the value is truncated to fit buf, always NUL-terminated, and
 * the full length is returned, so a return of size or more means it was cut.
 *
 * @param  name  Variable name.
 * @param  buf   Destination; may be NULL when size is 0, to ask for the length.
 * @param  size  Size of buf in bytes.
 * @return Length of the value (without the NUL), or -1 if it isn't set or name
 *         is invalid.
 */
int dino_env_get(const char *name, char *buf, size_t size);

/**
 * Set an environment variable of this process. Children spawned afterwards
 * with a NULL env inherit it. Not safe while other threads read or change the
 * environment.
 *
 * @param  name   Variable name: not empty, no '='.
 * @param  value  New value; may be empty.
 * @return 0 on success, -1 on failure or invalid arguments.
 */
int dino_env_set(const char *name, const char *value);

/**
 * Remove an environment variable of this process. Removing one that isn't set
 * succeeds.
 *
 * @param  name  Variable name: not empty, no '='.
 * @return 0 on success, -1 on failure or invalid arguments.
 */
int dino_env_unset(const char *name);

#ifdef __cplusplus
}
#endif

#endif // DINO_CORE_PROCESS_H
