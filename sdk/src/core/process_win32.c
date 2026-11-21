#include <dino/core/process.h>

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <windows.h>

#include <dino/core/time.h>

#include "pipe_internal.h"
#include "process_internal.h"

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

// CreateProcessW runs a .bat or .cmd file through cmd.exe, which splits the
// command line by its own rules: an unquoted & | < > ^ ( ) ends the argument
// and starts another command. Windows ignores trailing dots and spaces in the
// name, so "x.bat. " is a batch file too.
static int is_batch(const char *path)
{
    size_t n = strlen(path);
    while (n > 0 && (path[n - 1] == '.' || path[n - 1] == ' '))
        n--;
    return n >= 4
        && (_strnicmp(path + n - 4, ".bat", 4) == 0 || _strnicmp(path + n - 4, ".cmd", 4) == 0);
}

// cmd.exe from the system directory, never one found through the PATH, with
// AutoRun scripts off (/d) and delayed expansion off (/v:OFF) so ! is literal.
// Every argument is quoted, which makes the operators literal. What quoting
// can't protect is refused: " ends the quoted part, % expands variables even
// inside it, and a line break ends the command. /c strips the outer quotes.
static wchar_t *batch_command_line(char *const argv[], wchar_t **application)
{
    for (int i = 0; argv[i]; i++)
        if (strpbrk(argv[i], "\"%\r\n"))
            return NULL;
    struct buf b        = { 0 };
    const char prefix[] = "cmd.exe /d /e:ON /v:OFF /c \"";
    int        ok       = put(&b, prefix, sizeof(prefix) - 1) == 0;
    for (int i = 0; ok && argv[i]; i++)
        ok = (i == 0 || put(&b, " ", 1) == 0) && put(&b, "\"", 1) == 0
            && put(&b, argv[i], strlen(argv[i])) == 0 && put(&b, "\"", 1) == 0;
    ok = ok && put(&b, "\"", 1) == 0;

    wchar_t dir[MAX_PATH];
    UINT    len  = GetSystemDirectoryW(dir, MAX_PATH);
    *application = ok && len > 0 && len < MAX_PATH - 8 ? malloc((len + 9) * sizeof(wchar_t)) : NULL;
    wchar_t *w   = *application ? widen(b.data, (int)b.len) : NULL;
    free(b.data);
    if (!w)
    {
        free(*application);
        *application = NULL;
        return NULL;
    }
    wcscpy(*application, dir);
    wcscat(*application, L"\\cmd.exe");
    return w;
}

// "A=1\0B=2\0\0" in UTF-16.
// A program path with a separator but no root (./tool, bin\tool) is looked up
// in the child's working directory, as on Linux; CreateProcessW alone would
// look in the parent's. The match becomes lpApplicationName, so the child's
// argv[0] stays as given.
static int is_relative_path(const char *path)
{
    if (!strpbrk(path, "/\\") || path[0] == '/' || path[0] == '\\')
        return 0;
    return !(path[0] && path[1] == ':');
}

// The full path of dir\name if that is a file, or NULL.
static wchar_t *file_at(const wchar_t *dir, const wchar_t *name, const wchar_t *extension)
{
    size_t   len    = wcslen(dir) + 1 + wcslen(name) + wcslen(extension) + 1;
    wchar_t *joined = malloc(len * sizeof(wchar_t));
    if (!joined)
        return NULL;
    wcscpy(joined, dir);
    wcscat(joined, L"\\");
    wcscat(joined, name);
    wcscat(joined, extension);

    // Also resolves "." and "..", turns / into \, and makes a relative dir
    // absolute from the parent's directory, as the child's cwd will be.
    wchar_t *full = NULL;
    DWORD    need = GetFullPathNameW(joined, 0, NULL, NULL);
    if (need)
        full = malloc(need * sizeof(wchar_t));
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
    if (full && GetFullPathNameW(joined, need, full, NULL) < need)
        attributes = GetFileAttributesW(full);
    free(joined);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        free(full);
        return NULL;
    }
    return full;
}

// A relative program path as the child's working directory sees it: as given,
// or with .exe added. Not SearchPathW: for a name that starts with ".\" or
// "..\" it ignores the directory it is given and looks from the parent's.
static wchar_t *find_in(const wchar_t *dir, const char *program)
{
    wchar_t *name = widen(program, -1);
    if (!name)
        return NULL;
    wchar_t *found = file_at(dir, name, L"");
    if (!found)
        found = file_at(dir, name, L".exe");
    free(name);
    return found;
}

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
        if (dino__process_make_pipe(i, pipes, &end) != 0)
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

int dino_process_spawn(dino_process_t *process, char *const argv[],
    const dino_process_spawn_opts_t *opts, dino_process_pipes_t *pipes)
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
    dino_stdio_t modes[3];
    if (!argv || !argv[0] || !dino__process_modes(opts, modes))
        return -1;

    HANDLE   std[3] = { NULL, NULL, NULL };
    wchar_t *app    = NULL;
    wchar_t *cmd    = is_batch(argv[0]) ? batch_command_line(argv, &app) : command_line(argv);
    wchar_t *cwd    = opts && opts->cwd ? widen(opts->cwd, -1) : NULL;
    wchar_t *env    = opts && opts->env ? environment_block(opts->env) : NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs   = NULL;
    int                          started = 0;

    if (!cmd || (opts && opts->cwd && !cwd) || (opts && opts->env && !env))
        goto done;
    // A batch file's app is cmd.exe, which already finds it from the new cwd.
    if (!app && cwd && is_relative_path(argv[0]))
    {
        app = find_in(cwd, argv[0]);
        if (!app)
            goto done;
    }
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
    if (CreateProcessW(app, cmd, NULL, NULL, TRUE,
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
    free(app);
    free(cmd);
    free(cwd);
    free(env);
    if (!started)
        dino__process_close_parent_ends(pipes);
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
    if (TerminateProcess((HANDLE)process, DINO_EXIT_KILLED))
        return 0;
    // Fails with access denied once the process has exited. Nothing is left to
    // kill, which Linux reports as success.
    return WaitForSingleObject((HANDLE)process, 0) == WAIT_OBJECT_0 ? 0 : -1;
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
    if (!dino__process_valid_env_name(name) || (!buf && size))
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
    if (!dino__process_valid_env_name(name))
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

intptr_t dino__process_to_os(dino_process_t process)
{
    return (intptr_t)process;
}
