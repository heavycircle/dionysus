#include <stdio.h>

#include <dino/core/thread.h>

#define NUM_THREADS 4
// Small enough to finish in seconds under qemu, so CI runs it on every target.
#define COUNT 10000000

static int counter(void *arg)
{
    int id = (int)(intptr_t)arg;
    printf("  [Thread %d] Starting ....\n", id);

    int sum = 0;
    for (volatile int i = 0; i < COUNT; ++i)
        sum += 1;

    printf("  [Thread %d] Counted to %d\n", id, sum);
    return sum;
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

    long long total = 0;
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        int sum = 0;
        if (dino_thread_join(threads[i], &sum) != 0)
        {
            fprintf(stderr, "FATAL: Failed to join thread %d\n", i);
            return -1;
        }
        total += sum;
    }
    printf("Total: %lld\n", total);
    if (total != (long long)NUM_THREADS * COUNT)
    {
        fprintf(stderr, "FATAL: expected %lld\n", (long long)NUM_THREADS * COUNT);
        return 1;
    }
    return 0;
}
