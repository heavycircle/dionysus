// The request parser, tested without any sockets: pure input, pure output.
#include <string.h>
#include <utest.h>

#include "protocol.h"

struct protocol
{
    struct kv *db;
    char       out[KV_LINE_MAX];
};

UTEST_F_SETUP(protocol)
{
    utest_fixture->db = kv_create();
    ASSERT_TRUE(utest_fixture->db != NULL);
}

UTEST_F_TEARDOWN(protocol)
{
    kv_destroy(utest_fixture->db);
    EXPECT_TRUE(1);
}

#define RUN(line) kv_execute(utest_fixture->db, line, utest_fixture->out, KV_LINE_MAX)

UTEST_F(protocol, set_get_del_count)
{
    EXPECT_EQ(0, RUN("SET k v"));
    EXPECT_STREQ("OK", utest_fixture->out);
    EXPECT_EQ(0, RUN("GET k"));
    EXPECT_STREQ("VALUE v", utest_fixture->out);
    EXPECT_EQ(0, RUN("COUNT"));
    EXPECT_STREQ("COUNT 1", utest_fixture->out);
    EXPECT_EQ(0, RUN("DEL k"));
    EXPECT_STREQ("OK", utest_fixture->out);
    EXPECT_EQ(0, RUN("GET k"));
    EXPECT_STREQ("NOT_FOUND", utest_fixture->out);
    EXPECT_EQ(0, RUN("DEL k"));
    EXPECT_STREQ("NOT_FOUND", utest_fixture->out);
}

UTEST_F(protocol, values_keep_their_spaces)
{
    EXPECT_EQ(0, RUN("SET motd   hello  there"));
    EXPECT_EQ(0, RUN("GET motd"));
    EXPECT_STREQ("VALUE hello  there", utest_fixture->out);
}

UTEST_F(protocol, malformed_requests_are_errors)
{
    const char *bad[] = { "", "   ", "SET", "SET k", "SET k   ", "GET", "GET a b", "DEL", "COUNT x",
        "PUT k v", "get k" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        EXPECT_EQ(-1, RUN(bad[i]));
        EXPECT_EQ(0, strncmp(utest_fixture->out, "ERR ", 4));
    }
}

UTEST_F(protocol, overlong_request_is_rejected)
{
    char line[KV_LINE_MAX + 10];
    memset(line, 'a', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    EXPECT_EQ(-1, RUN(line));
    EXPECT_STREQ("ERR request too long", utest_fixture->out);
}

UTEST_MAIN()
