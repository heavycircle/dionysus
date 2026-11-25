#include <dino/core/process.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dino/core/time.h>

#include "error_internal.h"
#include "pipe_internal.h"
#include "process_internal.h"

extern char **environ;

// A process handle is its pid, carried in the pointer type: nothing is
// allocated and it is never dereferenced. A pid is never 0.
#define TO_HANDLE(pid)  ((dino_process_t)(uintptr_t)(pid))
#define TO_PID(process) ((pid_t)(uintptr_t)(process))

static dino_process_status_t decode_status(int status)
{
    dino_process_status_t s = { 0, 0 };
    if (WIFEXITED(status))
    {
        s.code = WEXITSTATUS(status);
    }
    else
    {
        s.signal = WTERMSIG(status);
        s.code   = 128 + s.signal;
    }
    return s;
}

// Without /proc (a chroot, an initramfs, a container that doesn't mount it):
// every descriptor up to the limit is checked instead, which is slower but
// bounded.
static int close_inherited_up_to_limit(posix_spawn_file_actions_t *actions)
{
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > (1L << 20))
        max = 1L << 20;
    for (long fd = 3; fd < max; fd++)
    {
        int flags = fcntl((int)fd, F_GETFD);
        if (flags >= 0 && !(flags & FD_CLOEXEC))
        {
            int rc = posix_spawn_file_actions_addclose(actions, (int)fd);
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

// Windows passes a child only the handles it lists. Here every descriptor
// without close-on-exec would reach it, so close those in the child, as
// posix_spawn_file_actions_addclosefrom_np would (musl has none). One that
// another thread opens meanwhile can slip through.
static int close_inherited(posix_spawn_file_actions_t *actions)
{
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return close_inherited_up_to_limit(actions);
    int            self = dirfd(dir);
    int            rc   = 0;
    struct dirent *e;
    while (rc == 0 && (e = readdir(dir)) != NULL)
    {
        char *end;
        long  fd = strtol(e->d_name, &end, 10);
        if (end == e->d_name || *end || fd < 3 || fd == self || fd > INT32_MAX)
            continue;
        int flags = fcntl((int)fd, F_GETFD);
        if (flags >= 0 && !(flags & FD_CLOEXEC))
            rc = posix_spawn_file_actions_addclose(actions, (int)fd);
    }
    closedir(dir);
    return rc;
}

int dino_process_spawn(dino_process_t *process, const char *const argv[],
    const dino_process_spawn_opts_t *opts, dino_process_pipes_t *pipes)
{
    if (!process)
        return dino__fail(DINO_EINVAL);
    *process = DINO_INVALID_PROCESS;
    dino_process_pipes_t unused;
    if (!pipes)
    {
        if (dino__process_any_pipe(opts))
            return dino__fail(DINO_EINVAL);
        pipes = &unused;
    }
    pipes->in = pipes->out = pipes->err = DINO_INVALID_PIPE;
    dino_stdio_t modes[3];
    dino_pipe_t  given[3];
    if (!argv || !argv[0] || !dino__process_modes(opts, modes, given)
        || !dino__process_valid_env(opts ? opts->env : NULL))
        return dino__fail(DINO_EINVAL);

    dino_pipe_t child[3] = { DINO_INVALID_PIPE, DINO_INVALID_PIPE, DINO_INVALID_PIPE };
    // A caller's pipe end on fd 0-2 (its own stdio was closed) would be
    // replaced by the child's other redirections before it is moved into
    // place: a copy above them is used instead, and closed after the spawn.
    int                        moved[3] = { -1, -1, -1 };
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t          attr;
    int                        started   = 0;
    int                        have_attr = 0;
    // The posix_spawn family returns its error. What failed is recorded at the
    // end, after the cleanup, whose own calls must not replace it.
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc != 0)
        return dino__fail_code(rc);
    int reason = DINO_EOTHER;
    // Which of the parent's own stdio is closed, looked at before any pipe is
    // made here (a pipe could take a closed one's number). An inherited stream
    // that's closed becomes /dev/null, as Windows gives NUL for a missing one,
    // so DINO_STDIO_STDOUT always has a stdout to follow.
    int closed[3];
    for (int fd = 0; fd < 3; fd++)
        closed[fd] = fcntl(fd, F_GETFD) < 0;
    for (int fd = 0; fd < 3; fd++)
    {
        rc = 0;
        if (modes[fd] == DINO_STDIO_PIPE)
        {
            if (dino__process_make_pipe(fd, pipes, &child[fd]) != 0)
            {
                reason = dino_error();
                goto done;
            }
            rc = posix_spawn_file_actions_adddup2(&actions, (int)dino__pipe_to_os(child[fd]), fd);
        }
        else if (modes[fd] == DINO_STDIO_NULL || (modes[fd] == DINO_STDIO_INHERIT && closed[fd]))
        {
            rc = posix_spawn_file_actions_addopen(
                &actions, fd, "/dev/null", fd == 0 ? O_RDONLY : O_WRONLY, 0);
        }
        else if (modes[fd] == DINO_STDIO_PIPE_END)
        {
            int source = (int)dino__pipe_to_os(given[fd]);
            if (source <= 2)
            {
                moved[fd] = fcntl(source, F_DUPFD_CLOEXEC, 3);
                if (moved[fd] < 0)
                {
                    reason = dino__error_from_os();
                    goto done;
                }
                source = moved[fd];
            }
            rc = posix_spawn_file_actions_adddup2(&actions, source, fd);
        }
        else if (modes[fd] == DINO_STDIO_STDOUT)
        {
            // After stdout's own redirection, so it follows wherever that went.
            rc = posix_spawn_file_actions_adddup2(&actions, 1, 2);
        }
        if (rc != 0)
            goto failed;
    }
    rc = close_inherited(&actions);
    if (rc == 0 && opts && opts->cwd)
        rc = posix_spawn_file_actions_addchdir_np(&actions, opts->cwd);
    if (rc != 0)
        goto failed;

    // The child starts with no signals blocked and SIGPIPE at its default, even
    // if the parent blocks or ignores them: a child that ignores SIGPIPE keeps
    // writing into a closed pipe.
    rc = posix_spawnattr_init(&attr);
    if (rc != 0)
        goto failed;
    have_attr = 1;
    sigset_t none, pipe_only;
    sigemptyset(&none);
    sigemptyset(&pipe_only);
    sigaddset(&pipe_only, SIGPIPE);
    rc = posix_spawnattr_setsigmask(&attr, &none);
    if (rc == 0)
        rc = posix_spawnattr_setsigdefault(&attr, &pipe_only);
    if (rc == 0)
        rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (rc != 0)
        goto failed;

    pid_t pid;
    // posix_spawnp reports a program that can't be run here, not as exit 127:
    // DINO_ENOENT for one that isn't found, DINO_EACCES for one that can't be
    // executed, and the same two for a cwd that can't be entered.
    // posix_spawnp takes char *const[] for history's sake (as execv does) and
    // never writes through it.
    char *const *args = (char *const *)(uintptr_t)argv;
    char *const *env  = opts && opts->env ? (char *const *)(uintptr_t)opts->env : environ;
    rc                = posix_spawnp(&pid, argv[0], &actions, &attr, args, env);
    if (rc == 0)
    {
        *process = TO_HANDLE(pid);
        started  = 1;
    }

failed:
    if (!started)
        reason = dino__error_from_code(rc);

done:
    if (have_attr)
        posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    for (int fd = 0; fd < 3; fd++)
    {
        if (child[fd] != DINO_INVALID_PIPE)
            dino_pipe_close(child[fd]);
        if (moved[fd] >= 0)
            close(moved[fd]);
    }
    if (!started)
        dino__process_close_parent_ends(pipes);
    return started ? 0 : dino__fail(reason);
}

int dino__process_spawn_shell(dino_process_t *process, const char *command,
    const dino_process_spawn_opts_t *opts, dino_process_pipes_t *pipes)
{
    // "--": a command starting with '-' is a command, not an option to sh, as
    // it is to cmd.exe.
    const char *const argv[] = { "/bin/sh", "-c", "--", command, NULL };
    return dino_process_spawn(process, argv, opts, pipes);
}

int dino_process_wait(dino_process_t process, dino_process_status_t *out)
{
    if (process == DINO_INVALID_PROCESS)
        return dino__fail(DINO_EINVAL);
    int status;
    while (waitpid(TO_PID(process), &status, 0) < 0)
        if (errno != EINTR)
            return dino__fail_os();
    if (out)
        *out = decode_status(status);
    return 0;
}

int dino_process_kill(dino_process_t process)
{
    if (process == DINO_INVALID_PROCESS)
        return dino__fail(DINO_EINVAL);
    return kill(TO_PID(process), SIGKILL) == 0 ? 0 : dino__fail_os();
}

// One small thread per detached child, ending when the child does: no state
// shared between children, and nothing left running once they're all gone.
static void *reap(void *arg)
{
    pid_t pid = (pid_t)(uintptr_t)arg;
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
    return NULL;
}

int dino_process_detach(dino_process_t process)
{
    if (process == DINO_INVALID_PROCESS)
        return dino__fail(DINO_EINVAL);
    pid_t pid = TO_PID(process);
    // Already gone: reaped here, no thread needed.
    if (waitpid(pid, NULL, WNOHANG) == pid)
        return 0;
    // A small stack: all the thread does is wait. glibc refuses one below its
    // own minimum plus the program's TLS, which can be more (under valgrind,
    // for one), so then the default size it is.
    // The thread starts with every signal blocked, so none meant for the
    // program is ever handled on it (or kills it by default there).
    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &old);
    pthread_attr_t attr;
    pthread_t      thread;
    int            rc = pthread_attr_init(&attr);
    if (rc == 0)
    {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_attr_setstacksize(&attr, (size_t)64 * 1024) == 0)
        {
            rc = pthread_create(&thread, &attr, reap, (void *)(uintptr_t)pid);
            if (rc == EINVAL)
            {
                pthread_attr_destroy(&attr);
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
                rc = pthread_create(&thread, &attr, reap, (void *)(uintptr_t)pid);
            }
        }
        else
        {
            rc = pthread_create(&thread, &attr, reap, (void *)(uintptr_t)pid);
        }
        pthread_attr_destroy(&attr);
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    return rc == 0 ? 0 : dino__fail_code(rc);
}

uint64_t dino_process_id(dino_process_t process)
{
    return (uint64_t)TO_PID(process);
}

int dino__pidfd_open(dino_process_t process)
{
#ifdef SYS_pidfd_open
    // The kernel always sets close-on-exec on a pidfd.
    return (int)syscall(SYS_pidfd_open, TO_PID(process), 0);
#else
    (void)process;
    errno = ENOSYS;
    return -1;
#endif
}

intptr_t dino__process_to_os(dino_process_t process)
{
    return (intptr_t)TO_PID(process);
}

// 1 once the child has exited, 0 on timeout, -1 if pidfds aren't available.
static int wait_pidfd(dino_process_t process, int timeout_ms)
{
    int fd = dino__pidfd_open(process);
    if (fd < 0)
        return -1;
    struct pollfd pfd      = { fd, POLLIN, 0 };
    uint64_t      deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    int           rc;
    while ((rc = poll(&pfd, 1, timeout_ms)) < 0 && errno == EINTR)
    {
        uint64_t now = dino_time_monotonic_ns();
        timeout_ms   = now >= deadline ? 0 : (int)((deadline - now + 999999) / 1000000);
    }
    close(fd);
    return rc < 0 ? -1 : rc > 0;
}

int dino_process_wait_timeout(dino_process_t process, int timeout_ms, dino_process_status_t *out)
{
    if (process == DINO_INVALID_PROCESS)
        return dino__fail(DINO_EINVAL);
    if (timeout_ms < 0)
        return dino_process_wait(process, out);

    int exited = wait_pidfd(process, timeout_ms);
    if (exited == 1)
        return dino_process_wait(process, out);
    if (exited == 0)
        return DINO_TIMEOUT;

    // No pidfds: poll waitpid, backing off to 10 ms.
    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    uint32_t delay_ms = 1;
    for (;;)
    {
        int   status;
        pid_t rc = waitpid(TO_PID(process), &status, WNOHANG);
        if (rc == TO_PID(process))
        {
            if (out)
                *out = decode_status(status);
            return 0;
        }
        if (rc < 0 && errno != EINTR)
            return dino__fail_os();

        uint64_t now = dino_time_monotonic_ns();
        if (now >= deadline)
            return DINO_TIMEOUT;
        uint64_t left_ms = (deadline - now + 999999) / 1000000;
        dino_sleep_ms(left_ms < delay_ms ? (uint32_t)left_ms : delay_ms);
        if (delay_ms < 10)
            delay_ms *= 2;
    }
}
