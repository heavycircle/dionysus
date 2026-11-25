#ifndef DINO_CORE_PROCESS_INTERNAL_H
#define DINO_CORE_PROCESS_INTERNAL_H

#include <stdint.h>
#include <string.h>

#include <dino/core/process.h>

#include "pipe_internal.h"

// Internal to the SDK: helpers process_posix.c and process_win32.c
// share. Inline, so each half compiles them in as it did when they were static
// in one file.

// The OS's own handle for a process: a pid on POSIX, a HANDLE on Windows.
intptr_t dino__process_to_os(dino_process_t process);

// dino_process_spawn for a shell command line (dino_process_run_shell): sh -c
// on POSIX, the command line passed to cmd.exe as written on Windows.
int dino__process_spawn_shell(dino_process_t *process, const char *command,
    const dino_process_spawn_opts_t *opts, dino_process_pipes_t *pipes);

// POSIX only (process_posix.c): a descriptor that becomes readable when the
// child exits, or -1 where pidfds aren't available (before Linux 5.3).
int dino__pidfd_open(dino_process_t process);

static inline int dino__process_valid_env_name(const char *name)
{
    return name && *name && !strchr(name, '=');
}

// 0 if an entry isn't NAME=value: Windows would end the block at an empty one
// and drop every later variable. A name may start with '=' (Windows keeps each
// drive's directory as "=C:=C:\dir").
static inline int dino__process_valid_env(const char *const *env)
{
    for (int i = 0; env && env[i]; i++)
        if (!env[i][0] || !strchr(env[i] + 1, '='))
            return 0;
    return 1;
}

// 0 if any mode is outside the enum, which each platform would otherwise
// treat its own way; DINO_STDIO_STDOUT anywhere but stderr; or a
// DINO_STDIO_PIPE_END end that isn't marked for a child (on Windows the child
// couldn't use it) or goes the wrong way. ends[i] is the end for stream i.
static inline int dino__process_modes(
    const dino_process_spawn_opts_t *opts, dino_stdio_t modes[3], dino_pipe_t ends[3])
{
    modes[0] = opts ? opts->stdin_mode : DINO_STDIO_INHERIT;
    modes[1] = opts ? opts->stdout_mode : DINO_STDIO_INHERIT;
    modes[2] = opts ? opts->stderr_mode : DINO_STDIO_INHERIT;
    ends[0]  = opts ? opts->stdin_pipe : DINO_INVALID_PIPE;
    ends[1]  = opts ? opts->stdout_pipe : DINO_INVALID_PIPE;
    ends[2]  = opts ? opts->stderr_pipe : DINO_INVALID_PIPE;
    for (int i = 0; i < 3; i++)
    {
        switch (modes[i])
        {
        case DINO_STDIO_INHERIT:
        case DINO_STDIO_NULL:
        case DINO_STDIO_PIPE:
            break;
        case DINO_STDIO_PIPE_END:
            if (!dino__pipe_for_child(ends[i]) || dino__pipe_is_read_end(ends[i]) != (i == 0))
                return 0;
            break;
        case DINO_STDIO_STDOUT:
            if (i != 2)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static inline int dino__process_any_pipe(const dino_process_spawn_opts_t *opts)
{
    return opts
        && (opts->stdin_mode == DINO_STDIO_PIPE || opts->stdout_mode == DINO_STDIO_PIPE
            || opts->stderr_mode == DINO_STDIO_PIPE);
}

// The parent's ends, filled in as pipes are made, so a failure can close them.
static inline void dino__process_close_parent_ends(dino_process_pipes_t *pipes)
{
    dino_pipe_t *ends[3] = { &pipes->in, &pipes->out, &pipes->err };
    for (int i = 0; i < 3; i++)
    {
        if (*ends[i] != DINO_INVALID_PIPE)
            dino_pipe_close(*ends[i]);
        *ends[i] = DINO_INVALID_PIPE;
    }
}

// For stream i (0 stdin, 1 stdout, 2 stderr): make a pipe, keep the parent's
// end in pipes, return the child's end.
static inline int dino__process_make_pipe(
    int i, dino_process_pipes_t *pipes, dino_pipe_t *child_end)
{
    dino_pipe_t *parent_end = i == 0 ? &pipes->in : i == 1 ? &pipes->out : &pipes->err;
    return dino__pipe_create_for_child(parent_end, child_end, i != 0);
}

#endif // DINO_CORE_PROCESS_INTERNAL_H
