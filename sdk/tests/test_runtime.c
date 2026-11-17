#include <utest.h>

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

UTEST_MAIN()
