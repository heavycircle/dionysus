#ifndef DINO_CORE_PROCESS_INTERNAL_H
#define DINO_CORE_PROCESS_INTERNAL_H

#include <dino/core/process.h>

// Internal to the SDK: helpers in process.c shared by process_posix.c and
// process_win32.c.

// Not empty and no '='.
int dino__process_valid_env_name(const char *name);
// The stdin, stdout and stderr modes from opts (all inherit when NULL).
void dino__process_modes(const dino_spawn_opts_t *opts, dino_stdio_t modes[3]);
// Whether opts asks for any pipe.
int dino__process_any_pipe(const dino_spawn_opts_t *opts);
// Closes the parent's pipe ends made so far, after a failed spawn.
void dino__process_close_parent_ends(dino_process_pipes_t *pipes);
// Makes a pipe for stream i (0 stdin, 1 stdout, 2 stderr): the parent's end
// goes in pipes, the child's end in child_end.
int dino__process_make_pipe(int i, dino_process_pipes_t *pipes, dino_pipe_t *child_end);

#endif // DINO_CORE_PROCESS_INTERNAL_H
