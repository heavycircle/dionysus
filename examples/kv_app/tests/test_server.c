// Integration test: the real server code over a real loopback socket, with a
// client and the server in one process. Runs unchanged under qemu and wine.
#include <string.h>
#include <utest.h>

#include <dino/core/network.h>

#include "server.h"

#define PORT 18500

struct server
{
    struct kv    *db;
    dino_socket_t listener, client, peer;
};

UTEST_F_SETUP(server)
{
    utest_fixture->db       = kv_create();
    utest_fixture->listener = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    utest_fixture->client   = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    dino_socket_set_reuseaddr(utest_fixture->listener, 1);
    ASSERT_EQ(0, dino_socket_bind(utest_fixture->listener, "127.0.0.1", PORT));
    ASSERT_EQ(0, dino_socket_listen(utest_fixture->listener, 1));
    ASSERT_EQ(0, dino_socket_connect(utest_fixture->client, "127.0.0.1", PORT));
    utest_fixture->peer = dino_socket_accept(utest_fixture->listener);
    ASSERT_NE(DINO_INVALID_SOCKET, utest_fixture->peer);
}

UTEST_F_TEARDOWN(server)
{
    if (utest_fixture->peer != DINO_INVALID_SOCKET)
        dino_socket_close(utest_fixture->peer);
    if (utest_fixture->client != DINO_INVALID_SOCKET)
        dino_socket_close(utest_fixture->client);
    dino_socket_close(utest_fixture->listener);
    kv_destroy(utest_fixture->db);
    EXPECT_TRUE(1);
}

// Sends one request, lets the server handle it, returns the response line.
static int roundtrip(struct server *s, const char *request, char *response, int len)
{
    int request_len = (int)strlen(request);
    if (dino_socket_send(s->client, request, request_len) != request_len)
        return -1;
    if (kv_serve_one(s->db, s->peer) != 0)
        return -1;
    memset(response, 0, (size_t)len);
    if (dino_socket_recv(s->client, response, len - 1) <= 0)
        return -1;
    response[strcspn(response, "\n")] = '\0';
    return 0;
}

UTEST_F(server, serves_a_session)
{
    char response[64];
    ASSERT_EQ(0, roundtrip(utest_fixture, "SET lang C\n", response, sizeof(response)));
    EXPECT_STREQ("OK", response);
    ASSERT_EQ(0, roundtrip(utest_fixture, "GET lang\n", response, sizeof(response)));
    EXPECT_STREQ("VALUE C", response);
    ASSERT_EQ(0, roundtrip(utest_fixture, "BOGUS\n", response, sizeof(response)));
    EXPECT_STREQ("ERR unknown command: BOGUS", response);
}

UTEST_F(server, accepts_crlf_line_endings)
{
    char response[64];
    ASSERT_EQ(0, roundtrip(utest_fixture, "SET k v\r\n", response, sizeof(response)));
    ASSERT_EQ(0, roundtrip(utest_fixture, "GET k\r\n", response, sizeof(response)));
    EXPECT_STREQ("VALUE v", response);
}

UTEST_F(server, reports_a_closed_connection)
{
    dino_socket_close(utest_fixture->client);
    utest_fixture->client = DINO_INVALID_SOCKET;
    EXPECT_EQ(1, kv_serve_one(utest_fixture->db, utest_fixture->peer));
}

UTEST_MAIN()
