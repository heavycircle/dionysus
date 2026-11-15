#include <dino/core/fs.h>
#include <inttypes.h>
#include <stdio.h>

#define BIG_FILE "bigfile.img"

int main(void)
{
    uint64_t size = dino_file_size(BIG_FILE);
    printf("Size of %s is %" PRIu64 "\n", BIG_FILE, size);

    return 0;
}
