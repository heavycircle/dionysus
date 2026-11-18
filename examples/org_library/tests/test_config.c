// Runs with `dino test -C examples/org_library`, not in projects that link the
// library. On Linux targets dino_test() also fails any test that leaks.
#include <org/config.h>
#include <utest.h>

#include <dino/util/logging.h>

UTEST(config, reads_keys_and_values)
{
    org_config_t *c = org_config_parse("name = billing\nport=8080\n");
    ASSERT_TRUE(c != NULL);
    EXPECT_EQ(2, org_config_count(c));
    EXPECT_STREQ("billing", org_config_get(c, "name", NULL));
    EXPECT_EQ(8080, org_config_get_long(c, "port", 0));
    org_config_free(c);
}

UTEST(config, missing_keys_use_the_fallback)
{
    org_config_t *c = org_config_parse("");
    ASSERT_TRUE(c != NULL);
    EXPECT_EQ(0, org_config_count(c));
    EXPECT_STREQ("default", org_config_get(c, "name", "default"));
    EXPECT_EQ(7, org_config_get_long(c, "port", 7));
    org_config_free(c);
}

UTEST(config, skips_comments_blank_and_malformed_lines)
{
    // The malformed lines are logged; keep the test output quiet.
    dino_log_set_level(DINO_LOG_LEVEL_ERROR);
    org_config_t *c = org_config_parse("# service\n\n  \nno equals sign\n= no key\n"
                                       "region = eu  # trailing comment\n");
    ASSERT_TRUE(c != NULL);
    EXPECT_EQ(1, org_config_count(c));
    EXPECT_STREQ("eu", org_config_get(c, "region", NULL));
    org_config_free(c);
}

UTEST(config, last_value_wins_and_empty_values_are_kept)
{
    org_config_t *c = org_config_parse("level = info\nlevel = debug\ntag =\n");
    ASSERT_TRUE(c != NULL);
    EXPECT_EQ(2, org_config_count(c));
    EXPECT_STREQ("debug", org_config_get(c, "level", NULL));
    EXPECT_STREQ("", org_config_get(c, "tag", "unset"));
    org_config_free(c);
}

UTEST(config, non_numbers_use_the_fallback)
{
    org_config_t *c = org_config_parse("port = 80x\nempty =\nbig = 99999999999999999999999\n"
                                       "negative = -3\n");
    ASSERT_TRUE(c != NULL);
    EXPECT_EQ(1, org_config_get_long(c, "port", 1));
    EXPECT_EQ(2, org_config_get_long(c, "empty", 2));
    EXPECT_EQ(3, org_config_get_long(c, "big", 3));
    EXPECT_EQ(-3, org_config_get_long(c, "negative", 0));
    org_config_free(c);
}

UTEST(config, handles_crlf_line_endings)
{
    org_config_t *c = org_config_parse("name = billing\r\nport = 9\r\n");
    ASSERT_TRUE(c != NULL);
    EXPECT_STREQ("billing", org_config_get(c, "name", NULL));
    EXPECT_EQ(9, org_config_get_long(c, "port", 0));
    org_config_free(c);
}

UTEST_MAIN()
