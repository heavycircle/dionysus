#include <dino/core/fs.h>
#include <stdint.h>
#include <stdio.h>

#define BIG_FLAG 0x40000000

#define BIG_FILE "bigfile.img"

int main(void)
{
    uint64_t size = dino_file_size(BIG_FILE);
    printf("Size of %s is %llu\n", BIG_FILE, size);

    size_t another_size = 420;
    printf("another_size = %zu\n", another_size);

    return 0;
}
