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

// POSIX only (process_posix.c): a descriptor that becomes readable when the
// child exits, or -1 where pidfds aren't available (before Linux 5.3).
int dino__pidfd_open(dino_process_t process);

static inline int dino__process_valid_env_name(const char *name)
{
    return name && *name && !strchr(name, '=');
}

// 0 if any mode is outside the enum, which each platform would otherwise
// treat its own way.
static inline int dino__process_modes(const dino_process_spawn_opts_t *opts, dino_stdio_t modes[3])
{
    modes[0] = opts ? opts->stdin_mode : DINO_STDIO_INHERIT;
    modes[1] = opts ? opts->stdout_mode : DINO_STDIO_INHERIT;
    modes[2] = opts ? opts->stderr_mode : DINO_STDIO_INHERIT;
    for (int i = 0; i < 3; i++)
        if (modes[i] != DINO_STDIO_INHERIT && modes[i] != DINO_STDIO_NULL
            && modes[i] != DINO_STDIO_PIPE)
            return 0;
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
