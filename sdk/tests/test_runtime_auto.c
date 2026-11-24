#include <stddef.h>
#include <utest.h>

#include <dino/core/fs.h>
#include <dino/core/network.h>
#include <dino/core/thread.h>

// Its own executable, so nothing has initialised the network before these run
// and the first tests really are the first use.

#define NUM_THREADS 8

static int create_socket(void *arg)
{
    dino_socket_t *slot = arg;
    *slot               = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    return 0;
}

UTEST(runtime_auto, first_use_from_many_threads_needs_no_init)
{
    dino_thread_t threads[NUM_THREADS];
    dino_socket_t socks[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
    {
        socks[i] = DINO_INVALID_SOCKET;
        ASSERT_EQ(0, dino_thread_create(&threads[i], create_socket, &socks[i]));
    }
    for (int i = 0; i < NUM_THREADS; i++)
        dino_thread_join(threads[i], NULL);

    for (int i = 0; i < NUM_THREADS; i++)
    {
        EXPECT_NE(DINO_INVALID_SOCKET, socks[i]);
        dino_socket_close(socks[i]);
    }
}

UTEST(runtime_auto, sockets_work_again_after_cleanup)
{
    dino_net_cleanup();
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    EXPECT_NE(DINO_INVALID_SOCKET, s);
    dino_socket_close(s);
}

UTEST(runtime_auto, explicit_init_is_repeatable)
{
    for (int i = 0; i < 3; i++)
    {
        EXPECT_EQ(0, dino_net_init());
        EXPECT_EQ(0, dino_fs_init());
    }
    dino_socket_t s = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    EXPECT_NE(DINO_INVALID_SOCKET, s);
    dino_socket_close(s);
    dino_net_cleanup();
}

#ifndef _WIN32
// Weak: referencing it doesn't pull it out of the archive, so it's only
// non-null if dino_core's link options did.
extern void dino__runtime_auto(void) __attribute__((weak));

UTEST(runtime_auto, setup_before_main_is_linked)
{
    EXPECT_TRUE(dino__runtime_auto != NULL);
}
#endif

UTEST_MAIN()
