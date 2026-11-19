#include <inttypes.h>
#include <stdio.h>

#include <dino/core/fs.h>

#define BIG_FILE "bigfile.img"

int main(void)
{
    uint64_t size;
    if (dino_file_size(BIG_FILE, &size) != 0)
    {
        printf("%s doesn't exist (create one to see its size)\n", BIG_FILE);
        return 0;
    }
    printf("Size of %s is %" PRIu64 "\n", BIG_FILE, size);
    return 0;
}
