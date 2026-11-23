#include <stdio.h>

#include <dino/core/network.h>
#include <dino/core/runtime.h>

/* The SDK needs no setup calls: sockets start the network on first use (see
   network_example). This program has also opted out of the setup before main()
   (CMakeLists.txt), so it does it explicitly, which is also the way to fail
   early with a clear message instead of on the first socket call. */
int main(void)
{
    if (dino_runtime_init() != 0)
    {
        fprintf(stderr, "dino runtime failed to start\n");
        return 1;
    }
    puts("runtime started");

    // Repeating init is harmless, and cleanup isn't final: the next socket
    // call starts the network again. It only runs while no socket is open.
    dino_runtime_init();
    if (dino_runtime_cleanup() != 0)
    {
        fprintf(stderr, "cleanup refused: a socket is open\n");
        return 1;
    }

    dino_socket_t sock = dino_socket_create(DINO_AF_INET, DINO_SOCK_STREAM, DINO_IPPROTO_TCP);
    if (sock == DINO_INVALID_SOCKET)
    {
        fprintf(stderr, "socket after cleanup failed\n");
        return 1;
    }
    puts("socket created after cleanup");
    dino_socket_close(sock);
    return 0;
}
