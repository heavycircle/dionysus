#include "main.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        LOG_ERROR("Usage: %s <filename>", argv[0]);
    }

    char write_buf[] = "Hello World!";

    int fd = open(argv[1], O_RDONLY);
    write(fd, write_buf, sizeof(write_buf));
    close(fd);

    return 0;
}
