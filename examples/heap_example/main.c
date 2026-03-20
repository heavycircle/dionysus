#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *buf = calloc(10, sizeof(char));
    buf = realloc(buf, 100);
    strncpy(buf, "Hello from this long string!", 5);
    printf("buf: %s\n", buf);

    char *another = malloc(50);
    strcpy(another, "Hello again!");
    printf("another: %s\n", another);

    float f = 2.345;
    printf("float: %.02f\n", f);

    free(buf);
    free(another);
    return 0;
}
