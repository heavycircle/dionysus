#include <stdio.h>
#include <string.h>

#include <dino/core/pipe.h>
#include <dino/core/process.h>

// Runs a shell command with its stdout on a pipe, prints what it wrote, and
// checks its exit code. The command line differs per OS; the dino calls don't.
#ifdef _WIN32
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

int main(void)
{
    char *const       argv[] = SHELL("echo hello from the child&& exit 3");
    dino_spawn_opts_t opts   = { 0 };
    opts.stdout_mode         = DINO_STDIO_PIPE;

    dino_process_t       child;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&child, argv, &opts, &pipes) != 0)
    {
        fprintf(stderr, "spawn failed\n");
        return 1;
    }
    printf("started process %llu\n", (unsigned long long)dino_process_id(child));

    char output[256];
    int  total = 0, n;
    while ((n = dino_pipe_read(pipes.out, output + total, (int)sizeof(output) - 1 - total)) > 0)
        total += n;
    output[total] = '\0';
    dino_pipe_close(pipes.out);

    int code = -1;
    if (dino_process_wait(child, &code) != 0)
        return 1;
    printf("child wrote: %s", output);
    printf("child exited with %d\n", code);
    return code == 3 && strstr(output, "hello from the child") ? 0 : 1;
}
