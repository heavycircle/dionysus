#include <dino/core/mutex.h>
#include <dino/core/thread.h>
#include <stdio.h>

#define NUM_THREADS 8
#define INCREMENTS  1000000

static dino_mutex_t g_mutex;
static long long    g_counter = 0;

DINO_THREAD_FUNC worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++)
    {
        dino_mutex_lock(g_mutex);
        g_counter++;
        dino_mutex_unlock(g_mutex);
    }
    DINO_THREAD_RETURN;
}

int main(void)
{
    g_mutex = dino_mutex_create();
    if (g_mutex == DINO_INVALID_MUTEX)
    {
        fprintf(stderr, "FATAL: Failed to create mutex\n");
        return -1;
    }

    dino_thread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
    {
        if (dino_thread_create(&threads[i], worker, NULL) != 0)
        {
            fprintf(stderr, "FATAL: Failed to create thread %d\n", i);
            return -1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++)
        dino_thread_join(threads[i]);

    long long expected = (long long)NUM_THREADS * INCREMENTS;
    printf("Expected: %lld\n", expected);
    printf("Got:      %lld\n", g_counter);
    printf("%s\n", g_counter == expected ? "OK" : "RACE DETECTED");

    dino_mutex_destroy(g_mutex);
    return 0;
}
