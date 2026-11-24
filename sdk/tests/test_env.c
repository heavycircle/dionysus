#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <utest.h>

#include <dino/core/process.h>

// setenv and unsetenv are the C library's on Linux and the SDK's on Windows,
// where MinGW has neither (sdk/src/libc/setenv_win32.c).

#define NAME "DINO_TEST_ENV_VAR"

#ifdef _WIN32
#define SHELL(cmd) { "cmd.exe", "/d", "/c", cmd, NULL }
#define SHOW_VAR   SHELL("echo [%" NAME "%]")
#else
#define SHELL(cmd) { "/bin/sh", "-c", cmd, NULL }
#define SHOW_VAR   SHELL("echo \"[${" NAME "-unset}]\"")
#endif

static void child_sees(int *utest_result, const char *expected)
{
    const char *const     argv[] = SHOW_VAR;
    dino_process_result_t r;
    ASSERT_EQ(0, dino_process_run(argv, NULL, &r));
    char *cr = strchr(r.out, '\r');
    if (cr)
        memmove(cr, cr + 1, strlen(cr));
    EXPECT_STREQ(expected, r.out);
    dino_process_result_free(&r);
}

UTEST(env, set_get_unset_and_children_see_it)
{
    EXPECT_EQ(0, setenv(NAME, "caf\xc3\xa9 au lait", 1));
    const char *value = getenv(NAME);
    ASSERT_TRUE(value != NULL);
    EXPECT_STREQ("caf\xc3\xa9 au lait", value);

    // ASCII for the child: cmd.exe echoes in the console code page, not UTF-8.
    EXPECT_EQ(0, setenv(NAME, "from parent", 1));
    child_sees(utest_result, "[from parent]\n");

    EXPECT_EQ(0, unsetenv(NAME));
    EXPECT_TRUE(getenv(NAME) == NULL);
    EXPECT_EQ(0, unsetenv(NAME));
#ifdef _WIN32
    // cmd.exe leaves an unknown %VAR% as is.
    child_sees(utest_result, "[%" NAME "%]\n");
#else
    child_sees(utest_result, "[unset]\n");
#endif
}

UTEST(env, overwrite_zero_keeps_a_set_value)
{
    ASSERT_EQ(0, setenv(NAME, "first", 0));
    EXPECT_STREQ("first", getenv(NAME));
    EXPECT_EQ(0, setenv(NAME, "second", 0));
    EXPECT_STREQ("first", getenv(NAME));
    EXPECT_EQ(0, setenv(NAME, "third", 1));
    EXPECT_STREQ("third", getenv(NAME));
    EXPECT_EQ(0, unsetenv(NAME));
}

UTEST(env, empty_value)
{
    ASSERT_EQ(0, setenv(NAME, "", 1));
#ifdef _WIN32
    // The C runtime can't hold an empty variable (docs/sdk/libc.md).
    EXPECT_TRUE(getenv(NAME) == NULL);
#else
    EXPECT_STREQ("", getenv(NAME));
    child_sees(utest_result, "[]\n");
#endif
    EXPECT_EQ(0, unsetenv(NAME));
}

UTEST(env, invalid_names_are_rejected_with_einval)
{
    errno = 0;
    EXPECT_EQ(-1, setenv("", "x", 1));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_EQ(-1, setenv("A=B", "x", 1));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_EQ(-1, unsetenv(""));
    EXPECT_EQ(EINVAL, errno);
    errno = 0;
    EXPECT_EQ(-1, unsetenv("A=B"));
    EXPECT_EQ(EINVAL, errno);
}

UTEST_MAIN()
