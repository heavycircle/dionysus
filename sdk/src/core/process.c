// posix_spawn_file_actions_addchdir_np.
#define _GNU_SOURCE

#include <dino/core/process.h>

#include <stdlib.h>

#include "pipe_handle.h"

static void modes_of(const dino_spawn_opts_t *opts, dino_stdio_t modes[3])
{
    modes[0] = opts ? opts->stdin_mode : DINO_STDIO_INHERIT;
    modes[1] = opts ? opts->stdout_mode : DINO_STDIO_INHERIT;
    modes[2] = opts ? opts->stderr_mode : DINO_STDIO_INHERIT;
}

static int any_pipe(const dino_spawn_opts_t *opts)
{
    return opts
        && (opts->stdin_mode == DINO_STDIO_PIPE || opts->stdout_mode == DINO_STDIO_PIPE
            || opts->stderr_mode == DINO_STDIO_PIPE);
}

// The parent's ends, filled in as pipes are made, so a failure can close them.
static void close_parent_ends(dino_process_pipes_t *pipes)
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
static int make_pipe(int i, dino_process_pipes_t *pipes, dino_pipe_t *child_end)
{
    dino_pipe_t r, w;
    if (dino_pipe_create(&r, &w) != 0)
        return -1;
    dino_pipe_t *parent_end = i == 0 ? &pipes->in : i == 1 ? &pipes->out : &pipes->err;
    *parent_end             = i == 0 ? w : r;
    *child_end              = i == 0 ? r : w;
    return 0;
}

#ifdef _WIN32

#include <string.h>

#include <windows.h>

struct buf
{
    char  *data;
    size_t len, cap;
};

static int put(struct buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap)
    {
        size_t cap  = (b->len + n + 1) * 2;
        char  *data = realloc(b->data, cap);
        if (!data)
            return -1;
        b->data = data;
        b->cap  = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int put_backslashes(struct buf *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (put(b, "\\", 1) != 0)
            return -1;
    return 0;
}

// The inverse of how the MSVCRT (and CommandLineToArgvW) split a command line:
// backslashes are literal unless they precede a quote, where each pair becomes
// one backslash and an odd one escapes the quote.
static int quote_arg(struct buf *b, const char *arg)
{
    if (*arg && !strpbrk(arg, " \t\n\v\""))
        return put(b, arg, strlen(arg));

    if (put(b, "\"", 1) != 0)
        return -1;
    for (const char *p = arg;; p++)
    {
        size_t backslashes = 0;
        while (*p == '\\')
        {
            p++;
            backslashes++;
        }
        int rc;
        if (!*p)
        {
            // Doubled so they don't escape the closing quote.
            rc = put_backslashes(b, backslashes * 2);
            if (rc == 0)
                rc = put(b, "\"", 1);
            return rc;
        }
        if (*p == '"')
            rc = put_backslashes(b, backslashes * 2 + 1);
        else
            rc = put_backslashes(b, backslashes);
        if (rc != 0 || put(b, p, 1) != 0)
            return -1;
    }
}

static wchar_t *widen(const char *s, int len)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t *w = malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (w)
    {
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, w, n);
        w[n] = L'\0';
    }
    return w;
}

static wchar_t *command_line(char *const argv[])
{
    struct buf b = { 0 };
    for (int i = 0; argv[i]; i++)
        if ((i > 0 && put(&b, " ", 1) != 0) || quote_arg(&b, argv[i]) != 0)
        {
            free(b.data);
            return NULL;
        }
    wchar_t *w = widen(b.data, (int)b.len);
    free(b.data);
    return w;
}

// "A=1\0B=2\0\0" in UTF-16.
static wchar_t *environment_block(char *const env[])
{
    struct buf b = { 0 };
    for (int i = 0; env[i]; i++)
        if (put(&b, env[i], strlen(env[i]) + 1) != 0)
        {
            free(b.data);
            return NULL;
        }
    // An empty environment still needs its double terminator.
    if (put(&b, "", 1) != 0)
    {
        free(b.data);
        return NULL;
    }
    wchar_t *w = widen(b.data, (int)b.len);
    free(b.data);
    return w;
}

// An inheritable duplicate, so the original stays private to the parent.
static HANDLE inheritable(HANDLE h)
{
    HANDLE dup = NULL;
    if (!DuplicateHandle(
            GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return NULL;
    return dup;
}

static HANDLE open_null(void)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE h = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa, OPEN_EXISTING, 0, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : h;
}

static const DWORD std_ids[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };

// The child's handle for stream i: always a new inheritable handle.
static HANDLE child_handle(int i, dino_stdio_t mode, dino_process_pipes_t *pipes)
{
    if (mode == DINO_STDIO_PIPE)
    {
        dino_pipe_t end;
        if (make_pipe(i, pipes, &end) != 0)
            return NULL;
        HANDLE h = inheritable(DINO__PIPE_TO_OS(end));
        dino_pipe_close(end);
        return h;
    }
    if (mode == DINO_STDIO_INHERIT)
    {
        HANDLE own = GetStdHandle(std_ids[i]);
        // GUI programs have no standard handles; the child gets NUL instead.
        if (own && own != INVALID_HANDLE_VALUE)
            return inheritable(own);
    }
    return open_null();
}

int dino_process_spawn(dino_process_t *process, char *const argv[], const dino_spawn_opts_t *opts,
    dino_process_pipes_t *pipes)
{
    *process = DINO_INVALID_PROCESS;
    dino_process_pipes_t unused;
    if (!pipes)
    {
        if (any_pipe(opts))
            return -1;
        pipes = &unused;
    }
    pipes->in = pipes->out = pipes->err = DINO_INVALID_PIPE;
    if (!argv || !argv[0])
        return -1;

    dino_stdio_t                 modes[3];
    HANDLE                       std[3]  = { NULL, NULL, NULL };
    wchar_t                     *cmd     = command_line(argv);
    wchar_t                     *cwd     = opts && opts->cwd ? widen(opts->cwd, -1) : NULL;
    wchar_t                     *env     = opts && opts->env ? environment_block(opts->env) : NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs   = NULL;
    int                          started = 0;

    modes_of(opts, modes);
    if (!cmd || (opts && opts->cwd && !cwd) || (opts && opts->env && !env))
        goto done;
    for (int i = 0; i < 3; i++)
    {
        std[i] = child_handle(i, modes[i], pipes);
        if (!std[i])
            goto done;
    }

    // Only these three handles reach the child, even if the parent has other
    // inheritable ones (bInheritHandles would otherwise pass them all).
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    attrs = malloc(size);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &size))
    {
        free(attrs);
        attrs = NULL;
        goto done;
    }
    if (!UpdateProcThreadAttribute(
            attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, (void *)std, sizeof(std), NULL, NULL))
        goto done;

    STARTUPINFOEXW si         = { 0 };
    si.StartupInfo.cb         = sizeof(si);
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = std[0];
    si.StartupInfo.hStdOutput = std[1];
    si.StartupInfo.hStdError  = std[2];
    si.lpAttributeList        = attrs;
    PROCESS_INFORMATION pi    = { 0 };
    if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, env, cwd, &si.StartupInfo,
            &pi))
    {
        CloseHandle(pi.hThread);
        *process = (dino_process_t)pi.hProcess;
        started  = 1;
    }

done:
    if (attrs)
    {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    for (int i = 0; i < 3; i++)
        if (std[i])
            CloseHandle(std[i]);
    free(cmd);
    free(cwd);
    free(env);
    if (!started)
        close_parent_ends(pipes);
    return started ? 0 : -1;
}

int dino_process_wait(dino_process_t process, int *exit_code)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    HANDLE h = (HANDLE)process;
    DWORD  code;
    if (WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(h, &code))
        return -1;
    CloseHandle(h);
    if (exit_code)
        *exit_code = (int)code;
    return 0;
}

int dino_process_kill(dino_process_t process)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    return TerminateProcess((HANDLE)process, DINO_EXIT_KILLED) ? 0 : -1;
}

uint64_t dino_process_id(dino_process_t process)
{
    return process == DINO_INVALID_PROCESS ? 0 : GetProcessId((HANDLE)process);
}

#else

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

int dino_process_spawn(dino_process_t *process, char *const argv[], const dino_spawn_opts_t *opts,
    dino_process_pipes_t *pipes)
{
    *process = DINO_INVALID_PROCESS;
    dino_process_pipes_t unused;
    if (!pipes)
    {
        if (any_pipe(opts))
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

    modes_of(opts, modes);
    if (posix_spawn_file_actions_init(&actions) != 0)
        return -1;
    for (int fd = 0; fd < 3; fd++)
    {
        int rc = 0;
        if (modes[fd] == DINO_STDIO_PIPE)
            rc = make_pipe(fd, pipes, &child[fd]) != 0
                || posix_spawn_file_actions_adddup2(&actions, DINO__PIPE_TO_OS(child[fd]), fd);
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
        close_parent_ends(pipes);
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
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
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

#endif // _WIN32
