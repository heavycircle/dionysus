// Before utest.h, which declares QueryPerformanceCounter itself on Windows.
#ifdef _WIN32
// clang-format off: <shellapi.h> needs <windows.h> first.
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utest.h>

#include <dino/core/pipe.h>
#include <dino/core/process.h>

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
static int run(char *const argv[], const dino_spawn_opts_t *base, char *out, int size)
{
    dino_spawn_opts_t opts = base ? *base : (dino_spawn_opts_t) { 0 };
    opts.stdout_mode       = DINO_STDIO_PIPE;
    dino_process_t       p;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&p, argv, &opts, &pipes) != 0)
        return -1;
    read_all(pipes.out, out, size);
    strip_cr(out);
    dino_pipe_close(pipes.out);
    int code = -1;
    if (dino_process_wait(p, &code) != 0)
        return -1;
    return code;
}

UTEST(process, exit_code_is_reported)
{
    char *const    argv[] = SHELL("exit 7");
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    ASSERT_NE(DINO_INVALID_PROCESS, p);
    EXPECT_NE(0u, dino_process_id(p));
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(p, &code));
    EXPECT_EQ(7, code);
}

UTEST(process, stdout_is_captured)
{
    char *const argv[] = SHELL("echo hello");
    char        out[64];
    EXPECT_EQ(0, run(argv, NULL, out, sizeof(out)));
    EXPECT_STREQ("hello\n", out);
}

UTEST(process, stdin_and_stderr_pipes)
{
#ifdef _WIN32
    char *const argv[] = { self_path, "--child-cat", NULL };
#else
    char *const argv[] = { "/bin/sh", "-c", "cat; echo oops >&2", NULL };
#endif
    dino_spawn_opts_t opts = { 0 };
    opts.stdin_mode        = DINO_STDIO_PIPE;
    opts.stdout_mode       = DINO_STDIO_PIPE;
    opts.stderr_mode       = DINO_STDIO_PIPE;
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
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(p, &code));
    EXPECT_EQ(0, code);
}

UTEST(process, null_stdio)
{
#ifdef _WIN32
    char *const argv[] = { self_path, "--child-cat", NULL };
#else
    char *const argv[] = { "/bin/sh", "-c", "cat", NULL };
#endif
    // stdin at end of file at once, so the copy ends; its output goes nowhere.
    dino_spawn_opts_t opts = { 0 };
    opts.stdin_mode        = DINO_STDIO_NULL;
    opts.stdout_mode       = DINO_STDIO_NULL;
    opts.stderr_mode       = DINO_STDIO_NULL;
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, &opts, NULL));
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(p, &code));
    EXPECT_EQ(0, code);
}

UTEST(process, working_directory)
{
#ifdef _WIN32
    char *const argv[] = SHELL("cd");
    const char *dir = "C:\\", *expected = "C:\\\n";
#else
    char *const argv[] = SHELL("pwd");
    const char *dir = "/", *expected = "/\n";
#endif
    dino_spawn_opts_t opts = { 0 };
    opts.cwd               = dir;
    char out[256];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
    EXPECT_STREQ(expected, out);
}

UTEST(process, environment_replaces_the_parents)
{
#ifdef _WIN32
    char *const argv[] = SHELL("echo %DINO_TEST_VAR%");
    // cmd.exe needs SYSTEMROOT; the parent's DINO_TEST_OTHER must not leak in.
    char systemroot[MAX_PATH + 16] = "SYSTEMROOT=";
    GetEnvironmentVariableA("SYSTEMROOT", systemroot + 11, MAX_PATH);
    char *const env[] = { "DINO_TEST_VAR=from env", systemroot, NULL };
#else
    char *const argv[] = SHELL("echo \"$DINO_TEST_VAR${HOME:+ home leaked}\"");
    char *const env[]  = { "DINO_TEST_VAR=from env", NULL };
#endif
    dino_spawn_opts_t opts = { 0 };
    opts.env               = env;
    char out[256];
    EXPECT_EQ(0, run(argv, &opts, out, sizeof(out)));
    EXPECT_STREQ("from env\n", out);
}

UTEST(process, arguments_arrive_unchanged)
{
    // Spaces, empty strings, quotes and backslashes: everything the MSVCRT
    // quoting rules must survive on Windows.
#ifdef _WIN32
    char *const argv[] = { self_path, "--child-echo", "plain", "two words", "", "quote\"inside",
        "trailing\\", "back\\\\slash \"q\"", "tab\tin", "\xc3\xa9t\xc3\xa9", NULL };
#else
    char *const argv[] = { "/bin/sh", "-c", "printf '[%s]\\n' \"$@\"", "sh", "plain", "two words",
        "", "quote\"inside", "trailing\\", "back\\\\slash \"q\"", "tab\tin", "\xc3\xa9t\xc3\xa9",
        NULL };
#endif
    char out[512];
    EXPECT_EQ(0, run(argv, NULL, out, sizeof(out)));
    EXPECT_STREQ("[plain]\n[two words]\n[]\n[quote\"inside]\n[trailing\\]\n"
                 "[back\\\\slash \"q\"]\n[tab\tin]\n[\xc3\xa9t\xc3\xa9]\n",
        out);
}

UTEST(process, kill_ends_a_running_child)
{
#ifdef _WIN32
    char *const argv[] = { self_path, "--child-sleep", NULL };
#else
    char *const argv[] = SHELL("sleep 60");
#endif
    dino_process_t p;
    ASSERT_EQ(0, dino_process_spawn(&p, argv, NULL, NULL));
    EXPECT_EQ(0, dino_process_kill(p));
    int code = -1;
    EXPECT_EQ(0, dino_process_wait(p, &code));
    EXPECT_EQ(DINO_EXIT_KILLED, code);
}

#ifndef _WIN32
UTEST(process, signal_is_reported_as_128_plus_its_number)
{
    char *const argv[] = SHELL("kill -TERM $$");
    char        out[8];
    EXPECT_EQ(128 + 15, run(argv, NULL, out, sizeof(out)));
}
#endif

UTEST(process, missing_program_fails_to_spawn)
{
    char *const          argv[] = { "dino-test-no-such-program", NULL };
    dino_process_t       p      = (dino_process_t)1;
    dino_spawn_opts_t    opts   = { 0 };
    dino_process_pipes_t pipes;
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
        int code = -1;
        EXPECT_EQ(0, dino_process_wait(p, &code));
        EXPECT_EQ(127, code);
        return;
    }
#endif
    EXPECT_EQ(-1, rc);
    EXPECT_EQ(DINO_INVALID_PROCESS, p);
    EXPECT_EQ(DINO_INVALID_PIPE, pipes.out);
}

UTEST(process, invalid_arguments_are_rejected)
{
    dino_process_t p;
    char *const    empty[] = { NULL };
    EXPECT_EQ(-1, dino_process_spawn(&p, NULL, NULL, NULL));
    EXPECT_EQ(-1, dino_process_spawn(&p, empty, NULL, NULL));

    // Asking for a pipe without somewhere to put it.
    char *const       argv[] = SHELL("exit 0");
    dino_spawn_opts_t opts   = { 0 };
    opts.stdout_mode         = DINO_STDIO_PIPE;
    EXPECT_EQ(-1, dino_process_spawn(&p, argv, &opts, NULL));

    EXPECT_EQ(-1, dino_process_wait(DINO_INVALID_PROCESS, NULL));
    EXPECT_EQ(-1, dino_process_kill(DINO_INVALID_PROCESS));
    EXPECT_EQ(0u, dino_process_id(DINO_INVALID_PROCESS));
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
    if (strcmp(mode, "--child-sleep") == 0)
    {
        Sleep(60000);
        return 0;
    }
    return 2;
}
#endif

int main(int argc, const char *const argv[])
{
#ifdef _WIN32
    if (argc > 1 && strncmp(argv[1], "--child-", 8) == 0)
        return child_main(argv[1]);
    GetModuleFileNameA(NULL, self_path, sizeof(self_path));
#endif
    return utest_main(argc, argv);
}
