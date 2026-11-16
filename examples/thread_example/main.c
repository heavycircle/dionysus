#include <stdio.h>

#include <dino/core/thread.h>

#define NUM_THREADS 4

DINO_THREAD_FUNC counter(void *arg)
{
    int id = (int)(intptr_t)arg;
    printf("  [Thread %d] Starting ....\n", id);

    int sum = 0;
    for (volatile int i = 0; i < 1000000000; ++i)
        sum += 1;

    printf("  [Thread %d] Counted to %d\n", id, sum);
    DINO_THREAD_RETURN;
}

int main(void)
{
    printf("Running with %d threads ...\n", NUM_THREADS);
    dino_thread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        if (dino_thread_create(&threads[i], counter, (void *)(intptr_t)i) != 0)
        {
            fprintf(stderr, "FATAL: Failed to create thread %d\n", i);
            return -1;
        }
    }

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        dino_thread_join(threads[i]);
    }
    return 0;
}
