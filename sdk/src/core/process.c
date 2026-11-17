// posix_spawn_file_actions_addchdir_np.
#define _GNU_SOURCE

#include <dino/core/process.h>

#include <stdlib.h>
#include <string.h>

#include <dino/core/thread.h>
#include <dino/core/time.h>

#include "pipe_internal.h"

static int valid_env_name(const char *name)
{
    return name && *name && !strchr(name, '=');
}

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
        HANDLE h = inheritable((HANDLE)dino__pipe_to_os(end));
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

// The timeout counts scheduler ticks and can end up to a tick early, so keep
// waiting until the monotonic deadline has really passed.
int dino_process_wait_timeout(dino_process_t process, int timeout_ms, int *exit_code)
{
    if (process == DINO_INVALID_PROCESS)
        return -1;
    if (timeout_ms < 0)
        return dino_process_wait(process, exit_code);

    uint64_t deadline = dino_time_monotonic_ns() + (uint64_t)timeout_ms * 1000000;
    for (;;)
    {
        uint64_t now  = dino_time_monotonic_ns();
        DWORD    wait = now >= deadline ? 0 : (DWORD)((deadline - now + 999999) / 1000000);
        DWORD    rc   = WaitForSingleObject((HANDLE)process, wait);
        if (rc == WAIT_OBJECT_0)
            return dino_process_wait(process, exit_code);
        if (rc != WAIT_TIMEOUT)
            return -1;
        if (dino_time_monotonic_ns() >= deadline)
            return DINO_TIMEOUT;
    }
}

uint64_t dino_process_current_id(void)
{
    return GetCurrentProcessId();
}

int dino_env_get(const char *name, char *buf, size_t size)
{
    if (!valid_env_name(name) || (!buf && size))
        return -1;
    wchar_t *wname = widen(name, -1);
    if (!wname)
        return -1;

    int      len   = -1;
    DWORD    n     = GetEnvironmentVariableW(wname, NULL, 0);
    wchar_t *value = n ? malloc(n * sizeof(wchar_t)) : NULL;
    if (value && GetEnvironmentVariableW(wname, value, n) < n)
    {
        len = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL) - 1;
        if (len >= 0 && size)
        {
            char *utf8 = malloc((size_t)len + 1);
            if (utf8)
            {
                WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, len + 1, NULL, NULL);
                size_t copy = (size_t)len < size ? (size_t)len : size - 1;
                memcpy(buf, utf8, copy);
                buf[copy] = '\0';
                free(utf8);
            }
            else
                len = -1;
        }
    }
    free(value);
    free(wname);
    return len;
}

// _wputenv_s keeps the C runtime's copy (getenv) in step, and
// SetEnvironmentVariableW sets what dino_env_get and children see. That one
// goes last: _wputenv_s takes an empty value to mean delete, from both copies,
// so an empty variable exists for Win32 and children but not for getenv.
static int env_change(const char *name, const char *value)
{
    if (!valid_env_name(name))
        return -1;
    wchar_t *wname  = widen(name, -1);
    wchar_t *wvalue = value ? widen(value, -1) : NULL;
    int      ok     = wname && (!value || wvalue) && _wputenv_s(wname, wvalue ? wvalue : L"") == 0
        && SetEnvironmentVariableW(wname, wvalue);
    free(wname);
    free(wvalue);
    return ok ? 0 : -1;
}

int dino_env_set(const char *name, const char *value)
{
    return value ? env_change(name, value) : -1;
}

int dino_env_unset(const char *name)
{
    return env_change(name, NULL);
}

#else

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

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
    if (!valid_env_name(name) || (!buf && size))
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
    if (!valid_env_name(name) || !value)
        return -1;
    return setenv(name, value, 1) == 0 ? 0 : -1;
}

int dino_env_unset(const char *name)
{
    if (!valid_env_name(name))
        return -1;
    return unsetenv(name) == 0 ? 0 : -1;
}

#endif // _WIN32

/* ---- Run to completion (both platforms) ---- */

// One thread per output pipe and one for the input: Windows can't poll pipes,
// and reading everything at once is what keeps a child that fills one pipe
// while the parent waits on another from deadlocking.
struct collector
{
    dino_pipe_t pipe;
    char       *data;
    size_t      len, cap;
    int         failed;
};

static int collect(void *arg)
{
    struct collector *c = arg;
    char              chunk[4096];
    int               n;
    // Keeps draining after running out of memory, so the child isn't blocked.
    while ((n = dino_pipe_read(c->pipe, chunk, sizeof(chunk))) > 0)
    {
        if (c->failed)
            continue;
        if (c->len + (size_t)n + 1 > c->cap)
        {
            size_t cap  = (c->len + (size_t)n + 1) * 2;
            char  *data = realloc(c->data, cap);
            if (!data)
            {
                c->failed = 1;
                continue;
            }
            c->data = data;
            c->cap  = cap;
        }
        memcpy(c->data + c->len, chunk, (size_t)n);
        c->len += (size_t)n;
        c->data[c->len] = '\0';
    }
    return 0;
}

struct feeder
{
    dino_pipe_t pipe;
    const char *data;
    size_t      len;
};

static int feed(void *arg)
{
    struct feeder *f = arg;
    // dino_pipe_write fails instead of raising SIGPIPE when the child has
    // stopped reading.
    for (size_t sent = 0; sent < f->len;)
    {
        size_t chunk = f->len - sent < 65536 ? f->len - sent : 65536;
        int    n     = dino_pipe_write(f->pipe, f->data + sent, (int)chunk);
        if (n <= 0)
            break;
        sent += (size_t)n;
    }
    dino_pipe_close(f->pipe);
    return 0;
}

static char *take(struct collector *c, size_t *len)
{
    char *data = c->data ? c->data : calloc(1, 1);
    *len       = c->data ? c->len : 0;
    c->data    = NULL;
    return data;
}

int dino_process_run(char *const argv[], const dino_run_opts_t *opts, dino_run_result_t *result)
{
    if (!result)
        return -1;
    memset(result, 0, sizeof(*result));
    result->exit_code = -1;

    dino_spawn_opts_t spawn = { 0 };
    spawn.cwd               = opts ? opts->cwd : NULL;
    spawn.env               = opts ? opts->env : NULL;
    spawn.stdin_mode        = opts && opts->input ? DINO_STDIO_PIPE : DINO_STDIO_NULL;
    spawn.stdout_mode       = DINO_STDIO_PIPE;
    spawn.stderr_mode       = DINO_STDIO_PIPE;

    dino_process_t       process;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&process, argv, &spawn, &pipes) != 0)
        return -1;

    struct collector out = { pipes.out, NULL, 0, 0, 0 };
    struct collector err = { pipes.err, NULL, 0, 0, 0 };
    struct feeder    in  = { pipes.in, opts ? opts->input : NULL, opts ? opts->input_len : 0 };
    dino_thread_t    threads[3] = { DINO_INVALID_THREAD, DINO_INVALID_THREAD, DINO_INVALID_THREAD };
    int              started    = dino_thread_create(&threads[0], collect, &out) == 0
        && dino_thread_create(&threads[1], collect, &err) == 0
        && (in.pipe == DINO_INVALID_PIPE || dino_thread_create(&threads[2], feed, &in) == 0);

    int status = 0;
    if (!started)
    {
        dino_process_kill(process);
        status = -1;
    }
    int timeout = opts && opts->timeout_ms > 0 ? opts->timeout_ms : -1;
    int rc      = dino_process_wait_timeout(process, timeout, &result->exit_code);
    if (rc == DINO_TIMEOUT)
    {
        dino_process_kill(process);
        rc     = dino_process_wait(process, &result->exit_code);
        status = status ? status : DINO_TIMEOUT;
    }
    if (rc != 0)
        status = -1;

    // The child is gone, so the readers reach end of file (unless a grandchild
    // still holds the pipes) and the writer's next write fails.
    for (int i = 0; i < 3; i++)
        if (threads[i] != DINO_INVALID_THREAD)
            dino_thread_join(threads[i], NULL);
    if (in.pipe != DINO_INVALID_PIPE && threads[2] == DINO_INVALID_THREAD)
        dino_pipe_close(in.pipe);
    dino_pipe_close(pipes.out);
    dino_pipe_close(pipes.err);

    result->out = take(&out, &result->out_len);
    result->err = take(&err, &result->err_len);
    if (out.failed || err.failed || !result->out || !result->err)
        status = -1;
    return status;
}

void dino_run_result_free(dino_run_result_t *result)
{
    if (!result)
        return;
    free(result->out);
    free(result->err);
    memset(result, 0, sizeof(*result));
}
