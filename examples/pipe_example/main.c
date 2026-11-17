#include <stdio.h>
#include <string.h>

#include <dino/core/pipe.h>
#include <dino/core/thread.h>

#define LINES 3

// A worker thread sends lines through a pipe and closes its end; the reader
// gets everything, then end of file. The same calls work across processes
// once a pipe end is a child's stdout.
static int send_lines(void *arg)
{
    dino_pipe_t w = *(dino_pipe_t *)arg;
    for (int i = 1; i <= LINES; i++)
    {
        char line[32];
        int  len = snprintf(line, sizeof(line), "line %d\n", i);
        if (dino_pipe_write(w, line, len) != len)
            return -1;
    }
    return dino_pipe_close(w);
}

int main(void)
{
    dino_pipe_t r, w;
    if (dino_pipe_create(&r, &w) != 0)
        return 1;

    dino_thread_t writer;
    if (dino_thread_create(&writer, send_lines, &w) != 0)
        return 1;

    char received[256] = { 0 };
    int  total         = 0, n;
    while (dino_pipe_wait(r, 5000) == 0
        && (n = dino_pipe_read(r, received + total, (int)sizeof(received) - 1 - total)) > 0)
        total += n;

    int result = -1;
    dino_thread_join(writer, &result);
    dino_pipe_close(r);

    printf("received %d bytes:\n%s", total, received);
    return result == 0 && strcmp(received, "line 1\nline 2\nline 3\n") == 0 ? 0 : 1;
}
