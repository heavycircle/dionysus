// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
// clang-format off: <shellapi.h> needs <windows.h> first.
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utest.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include <dino/core/error.h>
#include <dino/core/pipe.h>
#include <dino/core/process.h>
#include <dino/core/time.h>

// Linux children are /bin/sh: under qemu-user an emulated program can exec
// host binaries but not another emulated one. Windows children are cmd.exe or
// this test program itself, in one of the --child-* modes handled in main().

#ifdef _WIN32
static char self_path[1024];

#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#endif

// Reads a pipe to end of file into buf (NUL-terminated); returns the length.
static int read_all(dino_pipe_t p, char *buf, int size)
{
    int total = 0, n;
    while (total < size - 1 && (n = dino_pipe_read(p, buf + total, size - 1 - total)) > 0)
        total += n;
    buf[total] = '\0';
    return total;
}

// Drops the \r that Windows programs write before each \n.
static void strip_cr(char *s)
{
    char *out = s;
    for (; *s; s++)
        if (*s != '\r')
            *out++ = *s;
    *out = '\0';
}

// Runs argv with stdout piped; returns its exit code (or -1) and its output.
static int run(const char *const argv[], const dino_process_spawn_opts_t *base, char *out, int size)
{
    dino_process_spawn_opts_t opts = base ? *base : (dino_process_spawn_opts_t) { 0 };
    opts.stdout_mode               = DINO_STDIO_PIPE;
    dino_process_t       p;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&p, argv, &opts, &pipes) != 0)
        return -1;
    read_all(pipes.out, out, size);
    strip_cr(out);
    dino_pipe_close(pipes.out);
    dino_process_status_t status = { -1, 0 };
    if (dino_process_wait(p, &status) != 0)
        return -1;
    return status.code;
}

UTEST(process, exit_code_is_reported)
{
    const char *const argv[] = SHELL("exit 7");
    dino_process_t    p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    ASSERT_NE(DINO_INVALID_PROCESS, p);
    EXPECT_NE(0u, dino_process_id(p));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(7, status.code);
}

UTEST(process, stdout_is_captured)
{
    const char *const argv[] = SHELL("echo hello");
    char              out[64];
    EXPECT_EQ(0, run(argv, NULL, out, sizeof(out)));
    EXPECT_STREQ("hello\n", out);
}

UTEST(process, stdin_and_stderr_pipes)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-cat", NULL };
#else
    const char *const argv[] = { "/bin/sh", "-c", "cat; echo oops >&2", NULL };
#endif
    dino_process_spawn_opts_t opts = { 0 };
    opts.stdin_mode                = DINO_STDIO_PIPE;
    opts.stdout_mode               = DINO_STDIO_PIPE;
    opts.stderr_mode               = DINO_STDIO_PIPE;
    dino_process_t       p;
    dino_process_pipes_t pipes;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, &opts, &pipes));

    EXPECT_EQ(8, dino_pipe_write(pipes.in, "from in\n", 8));
    EXPECT_EQ(0, dino_pipe_close(pipes.in));
    char out[64], err[64];
    read_all(pipes.out, out, sizeof(out));
    read_all(pipes.err, err, sizeof(err));
    strip_cr(out);
    strip_cr(err);
    EXPECT_STREQ("from in\n", out);
    EXPECT_STREQ("oops\n", err);

    dino_pipe_close(pipes.out);
    dino_pipe_close(pipes.err);
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(0, status.code);
}

UTEST(process, null_stdio)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-cat", NULL };
#else
    const char *const argv[] = { "/bin/sh", "-c", "cat", NULL };
#endif
    // stdin at end of file at once, so the copy ends; its output goes nowhere.
    dino_process_spawn_opts_t opts = { 0 };
    opts.stdin_mode                = DINO_STDIO_NULL;
    opts.stdout_mode               = DINO_STDIO_NULL;
    opts.stderr_mode               = DINO_STDIO_NULL;
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, &opts, NULL));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(0, status.code);
}

#ifndef _WIN32
// A parent with stdin and stdout closed gets fds 0 and 1 for the stderr pipe,
// so the child's end sat at fd 1, where the child opens /dev/null for its
// stdout before it moves that end to fd 2.
UTEST(process, pipes_survive_a_parent_with_closed_stdio)
{
    int saved_in = dup(0), saved_out = dup(1);
    ASSERT_TRUE(saved_in >= 0 && saved_out >= 0);
    close(0);
    close(1);

    const char *const         argv[] = { "/bin/sh", "-c", "echo err >&2", NULL };
    dino_process_spawn_opts_t opts   = { 0 };
    opts.stdout_mode                 = DINO_STDIO_NULL;
    opts.stderr_mode                 = DINO_STDIO_PIPE;
    dino_process_t        p;
    dino_process_pipes_t  pipes;
    char                  err[64] = "";
    int                   spawned = dino_process_spawn(&p, argv, &opts, &pipes);
    dino_process_status_t status  = { -1, 0 };
    if (spawned == 0)
    {
        read_all(pipes.err, err, sizeof(err));
        dino_pipe_close(pipes.err);
        dino_process_wait(p, &status);
    }

    dup2(saved_in, 0);
    dup2(saved_out, 1);
    close(saved_in);
    close(saved_out);
    ASSERT_EQ(0, spawned);
    EXPECT_STREQ("err\n", err);
    EXPECT_EQ(0, status.code);
}
#endif

UTEST(process, working_directory)
{
#ifdef _WIN32
    const char *const argv[] = SHELL("cd");
    const char       *dir = "C:\\", *expected = "C:\\\n";
#else
    const char *const argv[] = SHELL("pwd");
    const char       *dir = "/", *expected = "/\n";
#endif
    dino_process_spawn_opts_t opts = { 0 };
    opts.cwd                       = dir;
    char out[256];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
    EXPECT_STREQ(expected, out);
}

#ifndef _WIN32
// Windows passes a child only the handles it lists; Linux children likewise get
// nothing but stdin, stdout and stderr, even of descriptors the program opened
// without close-on-exec.
UTEST(process, child_gets_only_the_standard_streams)
{
    int fd = dup(1);
    ASSERT_GE(fd, 3);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "[ -e /proc/self/fd/%d ] && echo inherited || echo closed", fd);
    const char *const argv[] = SHELL(cmd);
    char              out[64];
    EXPECT_EQ(0, run(argv, NULL, out, sizeof(out)));
    EXPECT_STREQ("closed\n", out);
    close(fd);
}
#endif

// A program path with a separator is found from the child's working
// directory, not the parent's. On Windows .exe is added, as for bare names.
UTEST(process, relative_program_is_found_from_cwd)
{
    dino_process_spawn_opts_t opts = { 0 };
#ifdef _WIN32
    // Run as "<its directory>\test_process" from that directory's parent.
    char parent[1024], prog[1024];
    snprintf(parent, sizeof(parent), "%s", self_path);
    char *file = strrchr(parent, '\\');
    ASSERT_TRUE(file != NULL);
    *file++   = '\0';
    char *dir = strrchr(parent, '\\');
    ASSERT_TRUE(dir != NULL);
    *dir++ = '\0';
    snprintf(prog, sizeof(prog), "%s/%s", dir, file);
    char *ext = strrchr(prog, '.');
    if (ext)
        *ext = '\0';
    const char *const argv[] = { prog, "--child-echo", "ok", NULL };
    opts.cwd                 = parent;
    const char *expected     = "[ok]\n";
#else
    const char *const argv[] = { "./sh", "-c", "echo ok", NULL };
    opts.cwd                 = "/bin";
    const char *expected     = "ok\n";
#endif
    char out[256];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
    EXPECT_STREQ(expected, out);
}

#ifdef _WIN32
// "./tool" with a cwd is the documented example. The parent stands somewhere
// else here, so looking from the parent's own directory can't find it, which
// is what Windows' own search does with a name that starts with a dot.
UTEST(process, dot_relative_program_is_found_from_cwd)
{
    char dir[1024], prog[1024], here[1024], elsewhere[1024];
    snprintf(dir, sizeof(dir), "%s", self_path);
    char *file = strrchr(dir, '\\');
    ASSERT_TRUE(file != NULL);
    *file++ = '\0';
    snprintf(prog, sizeof(prog), "./%s", file);
    char *ext = strrchr(prog, '.');
    if (ext && ext != prog)
        *ext = '\0';
    ASSERT_NE(0u, GetCurrentDirectoryA(sizeof(here), here));
    ASSERT_NE(0u, GetTempPathA(sizeof(elsewhere), elsewhere));

    const char *const         argv[] = { prog, "--child-echo", "ok", NULL };
    dino_process_spawn_opts_t opts   = { 0 };
    opts.cwd                         = dir;
    char out[256]                    = "";
    ASSERT_TRUE(SetCurrentDirectoryA(elsewhere));
    int rc = run(argv, &opts, out, sizeof(out));
    SetCurrentDirectoryA(here);

    EXPECT_EQ(0, rc);
    EXPECT_STREQ("[ok]\n", out);
}
#endif

UTEST(process, environment_replaces_the_parents)
{
#ifdef _WIN32
    const char *const argv[] = SHELL("echo %DINO_TEST_VAR%");
    // cmd.exe needs SYSTEMROOT; the parent's DINO_TEST_OTHER must not leak in.
    char        systemroot[MAX_PATH + 16] = "SYSTEMROOT=";
    const char *root                      = getenv("SYSTEMROOT");
    snprintf(systemroot + 11, MAX_PATH, "%s", root ? root : "");
    const char *const env[] = { "DINO_TEST_VAR=from env", systemroot, NULL };
#else
    const char *const argv[] = SHELL("echo \"$DINO_TEST_VAR${HOME:+ home leaked}\"");
    const char *const env[]  = { "DINO_TEST_VAR=from env", NULL };
#endif
    dino_process_spawn_opts_t opts = { 0 };
    opts.env                       = env;
    char out[256];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
    EXPECT_STREQ("from env\n", out);
}

// Windows ends its environment block at an empty entry, dropping every later
// variable, so both platforms refuse one, and one without '='.
UTEST(process, environment_entries_must_be_name_value)
{
    const char *const         argv[]  = SHELL("exit 0");
    const char *const         empty[] = { "A=1", "", "B=2", NULL };
    const char *const         no_eq[] = { "A=1", "JUSTNAME", NULL };
    dino_process_spawn_opts_t opts    = { 0 };
    dino_process_t            p;
    opts.env = empty;
    EXPECT_EQ(-1, dino_process_spawn(&p, argv, &opts, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());
    opts.env = no_eq;
    EXPECT_EQ(-1, dino_process_spawn(&p, argv, &opts, NULL));
    EXPECT_EQ(DINO_EINVAL, dino_error());

    // A name starting with '=' is how Windows keeps each drive's directory.
#ifdef _WIN32
    char        systemroot[MAX_PATH + 16] = "SYSTEMROOT=";
    const char *root                      = getenv("SYSTEMROOT");
    snprintf(systemroot + 11, MAX_PATH, "%s", root ? root : "");
    const char *const drive[] = { "=C:=C:\\", systemroot, NULL };
#else
    const char *const drive[] = { "=C:=C:\\", NULL };
#endif
    opts.env = drive;
    char out[16];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
}

UTEST(process, arguments_arrive_unchanged)
{
    // Spaces, empty strings, quotes and backslashes: everything the MSVCRT
    // quoting rules must survive on Windows.
#ifdef _WIN32
    const char *const argv[]
        = { self_path, "--child-echo", "plain", "two words", "", "quote\"inside", "trailing\\",
              "back\\\\slash \"q\"", "tab\tin", "\xc3\xa9t\xc3\xa9", NULL };
#else
    const char *const argv[] = { "/bin/sh", "-c", "printf '[%s]\\n' \"$@\"", "sh", "plain",
        "two words", "", "quote\"inside", "trailing\\", "back\\\\slash \"q\"", "tab\tin",
        "\xc3\xa9t\xc3\xa9", NULL };
#endif
    char out[512];
    EXPECT_EQ(0, run(argv, NULL, out, sizeof(out)));
    EXPECT_STREQ("[plain]\n[two words]\n[]\n[quote\"inside]\n[trailing\\]\n"
                 "[back\\\\slash \"q\"]\n[tab\tin]\n[\xc3\xa9t\xc3\xa9]\n",
        out);
}

#ifdef _WIN32
// cmd.exe runs batch files and parses their command line itself, where & | < >
// ^ ( ) start new commands unless quoted.
UTEST(process, batch_file_arguments_cant_run_commands)
{
    FILE *f = fopen("dino_test_args.cmd", "wb");
    ASSERT_TRUE(f != NULL);
    fputs("@echo off\r\necho [%1]\r\n", f);
    fclose(f);

    char              out[512];
    const char *const plain[] = { "dino_test_args.cmd", "a&echo INJECTED", NULL };
    EXPECT_EQ(0, run(plain, NULL, out, sizeof(out)));
    EXPECT_STREQ("[\"a&echo INJECTED\"]\n", out);

    const char *const specials[] = { "dino_test_args.CMD", "^(x)|y<z> !v!", NULL };
    EXPECT_EQ(0, run(specials, NULL, out, sizeof(out)));
    EXPECT_STREQ("[\"^(x)|y<z> !v!\"]\n", out);

    // What quoting can't protect is refused.
    dino_process_t    p;
    const char *const quote[]   = { "dino_test_args.cmd", "a\" & echo INJECTED & \"", NULL };
    const char *const percent[] = { "dino_test_args.cmd", "%PATH%", NULL };
    const char *const newline[] = { "dino_test_args.cmd", "a\necho INJECTED", NULL };
    const char *const dotted[]  = { "dino_test_args.cmd. ", "\"", NULL };
    EXPECT_EQ(-1, dino_process_spawn(&p, quote, NULL, NULL));
    EXPECT_EQ(-1, dino_process_spawn(&p, percent, NULL, NULL));
    EXPECT_EQ(-1, dino_process_spawn(&p, newline, NULL, NULL));
    EXPECT_EQ(-1, dino_process_spawn(&p, dotted, NULL, NULL));
    remove("dino_test_args.cmd");
}
#endif

UTEST(process, kill_ends_a_running_child)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    const char *const argv[] = SHELL("exec sleep 60");
#endif
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    EXPECT_EQ(0, dino_process_kill(p));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(DINO_EXIT_KILLED, status.code);
    EXPECT_EQ(9, status.signal);
}

// How a process ended, told apart the same way on every platform: an exit
// with a code, or a signal (on Windows, a crash). The Windows child ends with
// the status an access violation gives, without crashing for real, which
// would start a debugger under wine.
UTEST(process, exit_and_crash_are_told_apart)
{
#ifdef _WIN32
    const char *const exits[]   = { self_path, "--child-exit", "200", NULL };
    const char *const crashes[] = { self_path, "--child-exit", "0xC0000005", NULL };
#else
    const char *const exits[]   = SHELL("exit 200");
    const char *const crashes[] = SHELL("kill -SEGV $$");
#endif
    dino_process_t        p;
    dino_process_status_t status = { -1, -1 };
    ASSERT_EQ(0, dino_process_spawn(&p, exits, NULL, NULL));
    ASSERT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(200, status.code);
    EXPECT_EQ(0, status.signal);

    ASSERT_EQ(0, dino_process_spawn(&p, crashes, NULL, NULL));
    ASSERT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(SIGSEGV, status.signal);
#ifdef _WIN32
    EXPECT_EQ((int)0xC0000005, status.code);
#else
    EXPECT_EQ(128 + SIGSEGV, status.code);
#endif
}

#ifndef _WIN32
// Nothing is left to kill, which isn't an error; the exit code stands.
UTEST(process, kill_after_the_child_exited_succeeds)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-echo", NULL };
#else
    const char *const argv[] = SHELL("exit 0");
#endif
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    dino_sleep_ms(1500);
    EXPECT_EQ(0, dino_process_kill(p));
    dino_process_status_t status = { -1, 0 };
    ASSERT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(0, status.code);
}

UTEST(process, signal_is_reported_as_128_plus_its_number)
{
    const char *const argv[] = SHELL("kill -TERM $$");
    char              out[8];
    EXPECT_EQ(128 + 15, run(argv, NULL, out, sizeof(out)));
}
#endif

UTEST(process, missing_program_fails_to_spawn)
{
    const char *const         argv[] = { "dino-test-no-such-program", NULL };
    dino_process_t            p      = (dino_process_t)1;
    dino_process_spawn_opts_t opts   = { 0 };
    dino_process_pipes_t      pipes;
    opts.stdout_mode = DINO_STDIO_PIPE;
    int rc           = dino_process_spawn(&p, argv, &opts, &pipes);
#ifndef _WIN32
    // glibc's posix_spawn (the --memcheck build) reports a failed exec through
    // memory shared with a CLONE_VM child; valgrind runs that child as a plain
    // fork, so the error is lost and the child just exits 127.
    const char *preload = getenv("LD_PRELOAD");
    if (rc == 0 && preload && strstr(preload, "vgpreload"))
    {
        dino_pipe_close(pipes.out);
        dino_process_status_t status = { -1, 0 };
        EXPECT_EQ(0, dino_process_wait(p, &status));
        EXPECT_EQ(127, status.code);
        return;
    }
#endif
    EXPECT_EQ(-1, rc);
    EXPECT_EQ(DINO_INVALID_PROCESS, p);
    EXPECT_EQ(DINO_INVALID_PIPE, pipes.out);
}

UTEST(process, invalid_arguments_are_rejected)
{
    dino_process_t    p;
    const char *const empty[] = { NULL };
    EXPECT_EQ(-1, dino_process_spawn(&p, NULL, NULL, NULL));
    EXPECT_EQ(-1, dino_process_spawn(&p, empty, NULL, NULL));

    // Asking for a pipe without somewhere to put it.
    const char *const         argv[] = SHELL("exit 0");
    dino_process_spawn_opts_t opts   = { 0 };
    opts.stdout_mode                 = DINO_STDIO_PIPE;
    EXPECT_EQ(-1, dino_process_spawn(&p, argv, &opts, NULL));

    // Stdio modes outside the enum, on every stream.
    dino_process_pipes_t pipes;
    for (int stream = 0; stream < 3; stream++)
    {
        dino_stdio_t bad[] = { (dino_stdio_t)3, (dino_stdio_t)-1 };
        for (int i = 0; i < 2; i++)
        {
            dino_process_spawn_opts_t o                                                    = { 0 };
            *(stream == 0 ? &o.stdin_mode : stream == 1 ? &o.stdout_mode : &o.stderr_mode) = bad[i];
            EXPECT_EQ(-1, dino_process_spawn(&p, argv, &o, &pipes));
        }
    }

    EXPECT_EQ(-1, dino_process_spawn(NULL, argv, NULL, NULL));
    EXPECT_EQ(-1, dino_process_wait(DINO_INVALID_PROCESS, NULL));
    EXPECT_EQ(-1, dino_process_kill(DINO_INVALID_PROCESS));
    EXPECT_EQ(0u, dino_process_id(DINO_INVALID_PROCESS));
}

#ifdef _WIN32
// A parent whose standard handle is stale (it closed it, or was started with
// handles it didn't inherit) still spawns: the child gets NUL, as on POSIX.
UTEST(process, stale_standard_handle_inherits_nul)
{
    HANDLE saved = GetStdHandle(STD_OUTPUT_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, (HANDLE)(intptr_t)0x7ff0);
    const char *const argv[] = SHELL("exit 3");
    dino_process_t    p;
    int               rc = dino_process_spawn(&p, argv, NULL, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, saved);
    ASSERT_EQ(0, rc);
    dino_process_status_t status;
    EXPECT_EQ(0, dino_process_wait(p, &status));
    EXPECT_EQ(3, status.code);
}
#endif

/* ---- wait_timeout ---- */

UTEST(process, wait_timeout_times_out_then_reaps)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    const char *const argv[] = SHELL("exec sleep 60");
#endif
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    EXPECT_EQ(DINO_TIMEOUT, dino_process_wait_timeout(p, 0, NULL));
    uint64_t start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_process_wait_timeout(p, 50, NULL));
    EXPECT_GE((dino_time_monotonic_ns() - start) / 1000000, 50u);

    // Still valid after a timeout.
    EXPECT_EQ(0, dino_process_kill(p));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait_timeout(p, 10000, &status));
    EXPECT_EQ(DINO_EXIT_KILLED, status.code);
}

UTEST(process, wait_timeout_returns_the_exit_code)
{
    const char *const argv[] = SHELL("exit 5");
    dino_process_t    p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait_timeout(p, 10000, &status));
    EXPECT_EQ(5, status.code);
    EXPECT_EQ(-1, dino_process_wait_timeout(DINO_INVALID_PROCESS, 0, NULL));
}

UTEST(process, wait_timeout_returns_when_the_child_exits)
{
#ifdef _WIN32
    const char *const argv[] = SHELL("ping -n 1 127.0.0.1 >NUL & exit 6");
#else
    const char *const argv[] = SHELL("sleep 0.2; exit 6");
#endif
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    uint64_t              start  = dino_time_monotonic_ns();
    dino_process_status_t status = { -1, 0 };
    EXPECT_EQ(0, dino_process_wait_timeout(p, 20000, &status));
    EXPECT_EQ(6, status.code);
    // Long before the timeout: the wait ends with the child, not the clock.
    EXPECT_LT((dino_time_monotonic_ns() - start) / 1000000, 10000u);
}

/* ---- run ---- */

// Well past the 64 KB a pipe holds, on stdout and stderr at once: reading one
// to the end before the other would deadlock.
#define FLOOD_LINES 2000

UTEST(process, run_reads_both_outputs_without_deadlock)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-flood", NULL };
#else
    const char *const argv[]
        = SHELL("i=0; while [ $i -lt 2000 ]; do "
                "echo 0123456789012345678901234567890123456789012345678901234567890; "
                "echo abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij >&2; "
                "i=$((i+1)); done; exit 4");
#endif
    dino_process_result_t r;
    ASSERT_EQ(0, dino_process_run(argv, NULL, &r));
    EXPECT_EQ(4, r.status.code);
    // 61 and 60 characters, a newline, and a \r before it on Windows.
    size_t eol = 1;
#ifdef _WIN32
    eol = 2;
#endif
    EXPECT_EQ((size_t)FLOOD_LINES * (61 + eol), r.out_len);
    EXPECT_EQ((size_t)FLOOD_LINES * (60 + eol), r.err_len);
    EXPECT_EQ(r.out_len, strlen(r.out));
    dino_process_result_free(&r);
    EXPECT_EQ(NULL, r.out);
}

// Past max_output the rest is read and dropped: the child isn't blocked and
// runs to the end.
UTEST(process, run_caps_collected_output)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-flood", NULL };
#else
    const char *const argv[]
        = SHELL("i=0; while [ $i -lt 2000 ]; do "
                "echo 0123456789012345678901234567890123456789012345678901234567890; "
                "echo abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij >&2; "
                "i=$((i+1)); done; exit 4");
#endif
    dino_process_run_opts_t opts = { 0 };
    opts.max_output              = 1000;
    dino_process_result_t r;
    ASSERT_EQ(0, dino_process_run(argv, &opts, &r));
    EXPECT_EQ(4, r.status.code);
    EXPECT_EQ(1, r.truncated);
    EXPECT_EQ((size_t)1000, r.out_len);
    EXPECT_EQ((size_t)1000, r.err_len);
    EXPECT_EQ(r.out_len, strlen(r.out));
    EXPECT_EQ(0, strncmp(r.out, "0123456789", 10));
    dino_process_result_free(&r);

    opts.max_output = 1u << 20;
    ASSERT_EQ(0, dino_process_run(argv, &opts, &r));
    EXPECT_EQ(0, r.truncated);
    EXPECT_GT(r.out_len, (size_t)100000);
    dino_process_result_free(&r);
}

UTEST(process, run_feeds_input_while_reading)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-cat", NULL };
#else
    const char *const argv[] = { "/bin/sh", "-c", "cat; echo oops >&2", NULL };
#endif
    // Larger than a pipe in both directions.
    enum
    {
        SIZE = 200000
    };
    static char input[SIZE];
    for (int i = 0; i < SIZE; i++)
        input[i] = (i % 64 == 63) ? '\n' : (char)('a' + i % 26);

    dino_process_run_opts_t opts = { 0 };
    opts.input                   = input;
    opts.input_len               = SIZE;
    dino_process_result_t r;
    ASSERT_EQ(0, dino_process_run(argv, &opts, &r));
    EXPECT_EQ(0, r.status.code);
    strip_cr(r.out);
    EXPECT_EQ((size_t)SIZE, strlen(r.out));
    EXPECT_EQ(0, memcmp(input, r.out, SIZE));
    strip_cr(r.err);
    EXPECT_STREQ("oops\n", r.err);
    dino_process_result_free(&r);
}

UTEST(process, run_survives_a_child_that_ignores_its_input)
{
    // Exits without reading; the writer's EPIPE must not kill this process.
    const char *const argv[] = SHELL("exit 0");
    enum
    {
        SIZE = 1 << 20
    };
    char *input = calloc(1, SIZE);
    ASSERT_TRUE(input != NULL);
    dino_process_run_opts_t opts = { 0 };
    opts.input                   = input;
    opts.input_len               = SIZE;
    dino_process_result_t r;
    EXPECT_EQ(0, dino_process_run(argv, &opts, &r));
    EXPECT_EQ(0, r.status.code);
    dino_process_result_free(&r);
    free(input);
}

UTEST(process, run_kills_after_the_timeout)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    const char *const argv[] = SHELL("echo started; exec sleep 60");
#endif
    dino_process_run_opts_t opts = { 0 };
    opts.timeout_ms              = 200;
    dino_process_result_t r;
    uint64_t              start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_process_run(argv, &opts, &r));
    EXPECT_GE((dino_time_monotonic_ns() - start) / 1000000, 200u);
    EXPECT_EQ(DINO_EXIT_KILLED, r.status.code);
#ifndef _WIN32
    EXPECT_STREQ("started\n", r.out);
#endif
    dino_process_result_free(&r);
}

// The child exits at once, but a background process it started holds stdout
// open for seconds: run stops reading at its timeout.
UTEST(process, run_stops_reading_at_the_timeout_when_a_grandchild_holds_the_output)
{
#ifdef _WIN32
    const char *const argv[] = { self_path, "--child-start-linger", NULL };
    // A process can take most of a second to start under wine.
    int timeout = 1500;
#else
    const char *const argv[]  = SHELL("sleep 3 & echo hi");
    int               timeout = 300;
#endif
    dino_process_run_opts_t opts = { 0 };
    opts.timeout_ms              = timeout;
    dino_process_result_t r;
    uint64_t              start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_process_run(argv, &opts, &r));
    EXPECT_LT((dino_time_monotonic_ns() - start) / 1000000, (uint64_t)timeout + 1500);
    EXPECT_EQ(0, r.status.code);
    strip_cr(r.out);
    EXPECT_STREQ("hi\n", r.out);
    dino_process_result_free(&r);
}

#ifndef _WIN32
// The same for the input: a background process holds stdin without reading,
// so the writer would block once the pipe is full.
UTEST(process, run_stops_writing_at_the_timeout_when_a_grandchild_holds_the_input)
{
    // Through fd 3: sh gives a background job /dev/null as stdin before its own
    // redirections apply.
    const char *const argv[] = SHELL("exec 3<&0; sleep 3 <&3 3<&- >/dev/null 2>&1 & exit 0");
    enum
    {
        SIZE = 1 << 20
    };
    char *input = calloc(1, SIZE);
    ASSERT_TRUE(input != NULL);
    dino_process_run_opts_t opts = { 0 };
    opts.input                   = input;
    opts.input_len               = SIZE;
    opts.timeout_ms              = 300;
    dino_process_result_t r;
    uint64_t              start = dino_time_monotonic_ns();
    EXPECT_EQ(DINO_TIMEOUT, dino_process_run(argv, &opts, &r));
    EXPECT_LT((dino_time_monotonic_ns() - start) / 1000000, 2000u);
    EXPECT_EQ(0, r.status.code);
    dino_process_result_free(&r);
    free(input);
}
#endif

UTEST(process, run_reports_a_missing_program)
{
    const char *const     argv[] = { "dino-test-no-such-program", NULL };
    dino_process_result_t r;
    int                   rc = dino_process_run(argv, NULL, &r);
    // See missing_program_fails_to_spawn: under valgrind glibc can't tell.
    if (rc == 0)
        EXPECT_EQ(127, r.status.code);
    else
        EXPECT_EQ(-1, rc);
    dino_process_result_free(&r);
    EXPECT_EQ(-1, dino_process_run(argv, NULL, NULL));
}

UTEST_STATE();

#ifdef _WIN32
// The child side of the Windows tests. --child-echo splits the command line
// with CommandLineToArgvW, Windows' own parser, and prints each argument as
// UTF-8, so it checks the quoting and the UTF-16 conversion.
static int child_main(const char *mode)
{
    if (strcmp(mode, "--child-echo") == 0)
    {
        int       argc;
        wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 2; i < argc; i++)
        {
            char arg[256];
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, arg, sizeof(arg), NULL, NULL);
            printf("[%s]\n", arg);
        }
        LocalFree(wargv);
        return 0;
    }
    if (strcmp(mode, "--child-cat") == 0)
    {
        char buf[256];
        while (fgets(buf, sizeof(buf), stdin))
            fputs(buf, stdout);
        fputs("oops\n", stderr);
        return 0;
    }
    if (strcmp(mode, "--child-flood") == 0)
    {
        for (int i = 0; i < 2000; i++)
        {
            fputs("0123456789012345678901234567890123456789012345678901234567890\n", stdout);
            fputs("abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij\n", stderr);
        }
        return 4;
    }
    if (strcmp(mode, "--child-sleep") == 0)
    {
        dino_sleep_ms(60000);
        return 0;
    }
    if (strcmp(mode, "--child-start-linger") == 0)
    {
        // The grandchild inherits this process's stdout and outlives it.
        const char *const argv[] = { self_path, "--child-linger", NULL };
        dino_process_t    p;
        if (dino_process_spawn(&p, argv, NULL, NULL) != 0)
            return 1;
        puts("hi");
        return 0;
    }
    if (strcmp(mode, "--child-linger") == 0)
    {
        dino_sleep_ms(6000);
        return 0;
    }
    return 2;
}

// --child-exit <code>: end with that code, decimal or 0x hex, as the process's
// own exit status, which can be one an exception would give.
static int child_exit(const char *code)
{
    ExitProcess((UINT)strtoul(code, NULL, 0));
}
#endif

int main(int argc, const char *const argv[])
{
#ifdef _WIN32
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));
    if (argc > 2 && strcmp(argv[1], "--child-exit") == 0)
        return child_exit(argv[2]);
    if (argc > 1 && strncmp(argv[1], "--child-", 8) == 0)
        return child_main(argv[1]);
#endif
    return utest_main(argc, argv);
}
