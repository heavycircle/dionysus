// posix_spawn_file_actions_addchdir_np.
#define _GNU_SOURCE

#include <dino/core/process.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dino/core/time.h>

#include "pipe_internal.h"
#include "process_internal.h"

extern char **environ;

static int decode_status(int status)
{
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int dino_process_spawn(dino_process_t *process, char *const argv[], const dino_spawn_opts_t *opts,
    dino_process_pipes_t *pipes)
{
    *process = DINO_INVALID_PROCESS;
    dino_process_pipes_t unused;
    if (!pipes)
    {
        if (dino__process_any_pipe(opts))
            return -1;
        pipes = &unused;
    }
    pipes->in = pipes->out = pipes->err = DINO_INVALID_PIPE;
    if (!argv || !argv[0])
        return -1;

    dino_stdio_t modes[3];
    dino_pipe_t  child[3] = { DINO_INVALID_PIPE, DINO_INVALID_PIPE, DINO_INVALID_PIPE };
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t          attr;
    int                        started   = 0;
    int                        have_attr = 0;

    dino__process_modes(opts, modes);
    if (posix_spawn_file_actions_init(&actions) != 0)
        return -1;
    for (int fd = 0; fd < 3; fd++)
    {
        int rc = 0;
        if (modes[fd] == DINO_STDIO_PIPE)
            rc = dino__process_make_pipe(fd, pipes, &child[fd]) != 0
                || posix_spawn_file_actions_adddup2(&actions, (int)dino__pipe_to_os(child[fd]), fd);
        else if (modes[fd] == DINO_STDIO_NULL)
            rc = posix_spawn_file_actions_addopen(
                &actions, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
        if (rc != 0)
            goto done;
    }
    if (opts && opts->cwd && posix_spawn_file_actions_addchdir_np(&actions, opts->cwd) != 0)
        goto done;

    // The child starts with no signals blocked and SIGPIPE at its default, even
    // if the parent blocks or ignores them: a child that ignores SIGPIPE keeps
    // writing into a closed pipe.
    if (posix_spawnattr_init(&attr) != 0)
        goto done;
    have_attr = 1;
    sigset_t none, pipe_only;
    sigemptyset(&none);
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    if (posix_spawnattr_setsigmask(&attr, &none) != 0
        || posix_spawnattr_setsigdefault(&attr, &pipe_only) != 0
        || posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF) != 0)
        goto done;

    pid_t pid;
    // posix_spawnp reports a program that can't be run here, not as exit 127.
    if (posix_spawnp(&pid, argv[0], &actions, &attr, argv, opts && opts->env ? opts->env : environ)
        == 0)
    {
        *process = (dino_process_t)pid;
        started  = 1;
    }

done:
    if (have_attr)
        posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    for (int fd = 0; fd < 3; fd++)
        if (child[fd] != DINO_INVALID_PIPE)
            dino_pipe_close(child[fd]);
    if (!started)
        dino__process_close_parent_ends(pipes);
    return started ? 0 : -1;
}

int dino_process_wait(dino_process_t process, int *exit_code)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    int status;
    while (waitpid((pid_t)process, &status, 0) < 0)
        if (errno != EINTR)
            return -1;
    if (exit_code)
        *exit_code = decode_status(status);
    return 0;
}

int dino_process_kill(dino_process_t process)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    return kill((pid_t)process, SIGKILL) == 0 ? 0 : -1;
}

uint64_t dino_process_id(dino_process_t process)
{
    return (uint64_t)process;
}

// Polls with a short, growing sleep: pidfd_open would need Linux 5.3 and a
// fallback anyway.
int dino_process_wait_timeout(dino_process_t process, int timeout_ms, int *exit_code)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    if (timeout_ms < 0)
        return dino_process_wait(process, exit_code);

    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    uint32_t delay_ms = 1;
    for (;;)
    {
        int   status;
        pid_t rc = waitpid((pid_t)process, &status, WNOHANG);
        if (rc == (pid_t)process)
        {
            if (exit_code)
                *exit_code = decode_status(status);
            return 0;
        }
        if (rc < 0 && errno != EINTR)
            return -1;

        uint64_t now = dino_time_monotonic_ns();
        if (now >= deadline)
            return DINO_TIMEOUT;
        uint64_t left_ms = (deadline - now + 999999) / 1000000;
        dino_sleep_ms(left_ms < delay_ms ? (uint32_t)left_ms : delay_ms);
        if (delay_ms < 10)
            delay_ms *= 2;
    }
}

uint64_t dino_process_current_id(void)
{
    return (uint64_t)getpid();
}

int dino_env_get(const char *name, char *buf, size_t size)
{
    if (!dino__process_valid_env_name(name) || (!buf && size))
        return -1;
    const char *value = getenv(name);
    if (!value)
        return -1;
    size_t len = strlen(value);
    if (size)
    {
        size_t copy = len < size ? len : size - 1;
        memcpy(buf, value, copy);
        buf[copy] = '\0';
    }
    return (int)len;
}

int dino_env_set(const char *name, const char *value)
{
    if (!dino__process_valid_env_name(name) || !value)
        return -1;
    return setenv(name, value, 1) == 0 ? 0 : -1;
}

int dino_env_unset(const char *name)
{
    if (!dino__process_valid_env_name(name))
        return -1;
    return unsetenv(name) == 0 ? 0 : -1;
}
