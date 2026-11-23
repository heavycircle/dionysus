#include <utest.h>

#include <dino/core/error.h>
#include <dino/core/network.h>
#include <dino/core/runtime.h>

UTEST(runtime, init_makes_sockets_usable)
{
    ASSERT_EQ(0, dino_runtime_init());
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    EXPECT_NE(DINO_INVALID_SOCKET, s);
    dino_socket_close(s);
    dino_runtime_cleanup();
}

UTEST(runtime, init_and_cleanup_can_repeat)
{
    for (int i = 0; i < 3; i++)
    {
        ASSERT_EQ(0, dino_runtime_init());
        dino_runtime_cleanup();
    }
}

// On Windows a cleanup would break every open socket, so it is refused while
// any is open, on both platforms, and the socket keeps working.
UTEST(runtime, cleanup_is_refused_while_a_socket_is_open)
{
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_DGRAM, DINO_IPPROTO_UDP);
    ASSERT_NE(DINO_INVALID_SOCKET, s);
    EXPECT_EQ(-1, dino_runtime_cleanup());
    EXPECT_EQ(DINO_EINVAL, dino_error());
    EXPECT_EQ(-1, dino_net_cleanup());
    EXPECT_EQ(0, dino_socket_bind(s, "127.0.0.1", 0));
    EXPECT_EQ(2, dino_socket_sendto(s, "hi", 2, "127.0.0.1", 9));
    dino_socket_close(s);
    EXPECT_EQ(0, dino_runtime_cleanup());
}

UTEST_MAIN()
