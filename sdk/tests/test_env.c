#include <string.h>
#include <utest.h>

#include <dino/core/process.h>

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
    char *const       argv[] = SHOW_VAR;
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
    char buf[64];
    EXPECT_EQ(0, dino_env_set(NAME, "caf\xc3\xa9 au lait"));
    EXPECT_EQ(13, dino_env_get(NAME, buf, sizeof(buf)));
    EXPECT_STREQ("caf\xc3\xa9 au lait", buf);

    // ASCII for the child: cmd.exe echoes in the console code page, not UTF-8.
    EXPECT_EQ(0, dino_env_set(NAME, "from parent"));
    child_sees(utest_result, "[from parent]\n");

    EXPECT_EQ(0, dino_env_unset(NAME));
    EXPECT_EQ(-1, dino_env_get(NAME, buf, sizeof(buf)));
    EXPECT_EQ(0, dino_env_unset(NAME));
#ifdef _WIN32
    // cmd.exe leaves an unknown %VAR% as is.
    child_sees(utest_result, "[%" NAME "%]\n");
#else
    child_sees(utest_result, "[unset]\n");
#endif
}

UTEST(env, get_truncates_like_snprintf)
{
    ASSERT_EQ(0, dino_env_set(NAME, "0123456789"));
    char buf[4] = "zzz";
    EXPECT_EQ(10, dino_env_get(NAME, buf, sizeof(buf)));
    EXPECT_STREQ("012", buf);
    EXPECT_EQ(10, dino_env_get(NAME, NULL, 0));
    EXPECT_EQ(0, dino_env_unset(NAME));
}

UTEST(env, empty_value)
{
    char buf[8] = "zzz";
    ASSERT_EQ(0, dino_env_set(NAME, ""));
    EXPECT_EQ(0, dino_env_get(NAME, buf, sizeof(buf)));
    EXPECT_STREQ("", buf);
    EXPECT_EQ(0, dino_env_unset(NAME));
}

UTEST(env, invalid_names_are_rejected)
{
    char buf[8];
    EXPECT_EQ(-1, dino_env_set(NULL, "x"));
    EXPECT_EQ(-1, dino_env_set("", "x"));
    EXPECT_EQ(-1, dino_env_set("A=B", "x"));
    EXPECT_EQ(-1, dino_env_set(NAME, NULL));
    EXPECT_EQ(-1, dino_env_get("A=B", buf, sizeof(buf)));
    EXPECT_EQ(-1, dino_env_get(NAME, NULL, 4));
    EXPECT_EQ(-1, dino_env_unset(""));
}

UTEST_MAIN()
