#include <stdio.h>
#include <string.h>

#include <dino/core/pipe.h>
#include <dino/core/process.h>

// Runs a shell command two ways and checks its output and exit code. The
// command line differs per OS; the dino calls don't.
#ifdef _WIN32
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

// The simple case: run to completion, collecting stdout and stderr.
static int run_and_collect(void)
{
    char *const       argv[] = SHELL("echo hello from the child&& echo a warning 1>&2&& exit 3");
    dino_process_run_opts_t   opts   = { 0 };
    dino_process_result_t result;
    opts.timeout_ms = 10000;
    if (dino_process_run(argv, &opts, &result) != 0)
    {
        fprintf(stderr, "run failed\n");
        dino_process_result_free(&result);
        return 0;
    }
    printf("run: exit %d, stdout: %s", result.exit_code, result.out);
    printf("run: stderr: %s", result.err);
    int ok = result.exit_code == 3 && strstr(result.out, "hello from the child")
        && strstr(result.err, "a warning");
    dino_process_result_free(&result);
    return ok;
}

// Streaming: spawn with a pipe and read the output as it arrives.
static int spawn_and_stream(void)
{
    char *const       argv[] = SHELL("echo hello from the child&& exit 3");
    dino_process_spawn_opts_t opts   = { 0 };
    opts.stdout_mode         = DINO_STDIO_PIPE;

    dino_process_t       child;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&child, argv, &opts, &pipes) != 0)
    {
        fprintf(stderr, "spawn failed\n");
        return 0;
    }
    printf("spawn: started process %llu\n", (unsigned long long)dino_process_id(child));

    char output[256];
    int  total = 0, n;
    while ((n = dino_pipe_read(pipes.out, output + total, (int)sizeof(output) - 1 - total)) > 0)
        total += n;
    output[total] = '\0';
    dino_pipe_close(pipes.out);

    int code = -1;
    if (dino_process_wait(child, &code) != 0)
        return 0;
    printf("spawn: exit %d, stdout: %s", code, output);
    return code == 3 && strstr(output, "hello from the child") != NULL;
}

int main(void)
{
    int ok = run_and_collect();
    ok &= spawn_and_stream();
    return ok ? 0 : 1;
}
