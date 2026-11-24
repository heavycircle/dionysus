#include <dino/core/network.h>

// Internal, not API (the dino__ prefix, CONTRIBUTING.md#design-rules). The
// SDK's Windows <sys/socket.h> references dino__winsock_auto, which pulls this
// object, and its constructor, out of the archive: Winsock is started before
// main() only in programs that include a socket header, so raw calls such as
// getaddrinfo work before any dino_socket_create. A failure can't be reported
// here; the first socket call then fails with WSANOTINITIALISED, and
// dino_net_init reports why.
int dino__winsock_auto;

__attribute__((constructor(101))) static void dino__winsock_start(void)
{
    dino_net_init();
}
