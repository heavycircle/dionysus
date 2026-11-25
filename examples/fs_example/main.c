#include <inttypes.h>
#include <stdio.h>

#include <dino/core/error.h>
#include <dino/core/fs.h>

#define SMALL_FILE "fs_example.tmp"
#define BIG_FILE   "bigfile.img"

int main(void)
{
    // A file of a known size, written with plain stdio, and read back.
    FILE *f       = fopen(SMALL_FILE, "wb");
    int   written = f && fwrite("hello, sizes", 1, 12, f) == 12;
    if (f && fclose(f) != 0)
        written = 0;
    if (!written)
    {
        printf("Could not write %s (FAILED)\n", SMALL_FILE);
        return 1;
    }
    uint64_t size = 0;
    int      ok   = dino_file_size(SMALL_FILE, &size) == 0 && size == 12;
    printf("Size of %s is %" PRIu64 "%s\n", SMALL_FILE, size, ok ? "" : " (FAILED: expected 12)");
    remove(SMALL_FILE);

    // Make one past 4 GB to see sizes no 32-bit API can report:
    //   truncate -s 5G bigfile.img        (Linux)
    //   fsutil file createnew bigfile.img 5368709120   (Windows)
    if (dino_file_size(BIG_FILE, &size) == 0)
        printf("Size of %s is %" PRIu64 "\n", BIG_FILE, size);
    else
        printf("No size for %s: %s\n", BIG_FILE, dino_error_message(dino_error()));
    return ok ? 0 : 1;
}
