#include <stdio.h>

#include <dino/core/mutex.h>
#include <dino/core/thread.h>

#define NUM_THREADS 8
#define INCREMENTS  1000000

static dino_mutex_t g_mutex;
static long long    g_counter = 0;

// One slot per thread: a shared error count would itself need the mutex.
static int g_errors[NUM_THREADS];

DINO_THREAD_FUNC worker(void *arg)
{
    int *errors = arg;
    for (int i = 0; i < INCREMENTS; i++)
    {
        if (dino_mutex_lock(g_mutex) != 0)
        {
            (*errors)++;
            continue;
        }
        g_counter++;
        if (dino_mutex_unlock(g_mutex) != 0)
            (*errors)++;
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
        if (dino_thread_create(&threads[i], worker, &g_errors[i]) != 0)
        {
            fprintf(stderr, "FATAL: Failed to create thread %d\n", i);
            return -1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++)
        dino_thread_join(threads[i]);

    int errors = 0;
    for (int i = 0; i < NUM_THREADS; i++)
        errors += g_errors[i];

    long long expected = (long long)NUM_THREADS * INCREMENTS;
    printf("Expected: %lld\n", expected);
    printf("Got:      %lld\n", g_counter);
    if (errors)
        printf("Lock/unlock errors: %d\n", errors);
    int ok = g_counter == expected && errors == 0;
    printf("%s\n", errors ? "FAILED" : (g_counter != expected ? "RACE DETECTED" : "OK"));

    dino_mutex_destroy(g_mutex);
    return ok ? 0 : 1;
}
