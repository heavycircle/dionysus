#include <stdio.h>

#include <dino/core/fs.h>
#include <dino/core/network.h>

/* The SDK needs no setup calls: sockets start the network on first use (see
   network_example). This program has opted out of the setup before main()
   (CMakeLists.txt), so it points closed stdio at /dev/null itself, and starts
   the network up front to fail early with a clear message instead of on the
   first socket call. */
int main(void)
{
    if (dino_fs_init() != 0)
        return 1;
    if (dino_net_init() != 0)
    {
        fprintf(stderr, "network failed to start\n");
        return 1;
    }
    puts("runtime started");

    dino_socket_t sock = dino_socket_create(AF_INET, SOCK_STREAM, 0);
    if (sock == DINO_INVALID_SOCKET)
    {
        fprintf(stderr, "socket failed\n");
        return 1;
    }
    puts("socket created");
    dino_socket_close(sock);
    return 0;
}
