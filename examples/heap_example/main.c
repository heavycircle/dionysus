#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *buf = calloc(10, sizeof(char));
    if (!buf)
        return 1;
    char *grown = realloc(buf, 100);
    if (!grown)
    {
        free(buf);
        return 1;
    }
    buf = grown;
    // calloc zero-filled the first 10 bytes, so the 5 copied are terminated.
    strncpy(buf, "Hello from this long string!", 5);
    printf("buf: %s\n", buf);

    char *another = malloc(50);
    if (!another)
    {
        free(buf);
        return 1;
    }
    strcpy(another, "Hello again!");
    printf("another: %s\n", another);

    float f = 2.345f;
    char  text[16];
    snprintf(text, sizeof(text), "%.02f", f);
    printf("float: %s\n", text);

    int ok = strcmp(buf, "Hello") == 0 && strcmp(another, "Hello again!") == 0
        && strcmp(text, "2.35") == 0;
    free(buf);
    free(another);
    if (!ok)
        fprintf(stderr, "heap_example: unexpected result\n");
    return ok ? 0 : 1;
}
