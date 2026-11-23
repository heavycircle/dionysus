#include <dino/core/runtime.h>

#include <dino/core/fs.h>
#include <dino/core/network.h>

int dino_runtime_init(void)
{
    if (dino_fs_init() != 0)
        return -1;
    if (dino_net_init() != 0)
        return -1;

    return 0;
}

int dino_runtime_cleanup(void)
{
    return dino_net_cleanup();
}
