#ifndef DINO_CORE_PROCESS_INTERNAL_H
#define DINO_CORE_PROCESS_INTERNAL_H

#include <string.h>

#include <dino/core/process.h>

#include "pipe_internal.h"

// Internal to the SDK: helpers process_posix.c and process_win32.c
// share. Inline, so each half compiles them in as it did when they were static
// in one file.

static inline int dino__process_valid_env_name(const char *name)
{
    return name && *name && !strchr(name, '=');
}

static inline void dino__process_modes(const dino_spawn_opts_t *opts, dino_stdio_t modes[3])
{
    modes[0] = opts ? opts->stdin_mode : DINO_STDIO_INHERIT;
    modes[1] = opts ? opts->stdout_mode : DINO_STDIO_INHERIT;
    modes[2] = opts ? opts->stderr_mode : DINO_STDIO_INHERIT;
}

static inline int dino__process_any_pipe(const dino_spawn_opts_t *opts)
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
