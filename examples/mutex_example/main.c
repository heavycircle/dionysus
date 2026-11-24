#include <pthread.h>
#include <stdio.h>

#include <dino/core/thread.h>

/* Mutexes and condition variables are plain pthreads on every target. The SDK
   adds one thing: a timed wait that follows the monotonic clock
   (dino_cond_init and dino_cond_timedwait), which pthreads can't give on
   Windows. */

#define NUM_THREADS 8
#define INCREMENTS  1000000

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_done;
static long long       g_counter  = 0;
static int             g_finished = 0;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++)
    {
        pthread_mutex_lock(&g_mutex);
        g_counter++;
        pthread_mutex_unlock(&g_mutex);
    }
    pthread_mutex_lock(&g_mutex);
    g_finished++;
    pthread_cond_signal(&g_done);
    pthread_mutex_unlock(&g_mutex);
    return NULL;
}

int main(void)
{
    if (dino_cond_init(&g_done) != 0)
    {
        fprintf(stderr, "FATAL: Failed to create condition variable\n");
        return -1;
    }

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++)
    {
        if (pthread_create(&threads[i], NULL, worker, NULL) != 0)
        {
            fprintf(stderr, "FATAL: Failed to create thread %d\n", i);
            return -1;
        }
    }

    // Wait for every worker, giving up after a minute without progress.
    int timed_out = 0;
    pthread_mutex_lock(&g_mutex);
    while (g_finished < NUM_THREADS && !timed_out)
        timed_out = dino_cond_timedwait(&g_done, &g_mutex, 60000) == DINO_TIMEOUT;
    pthread_mutex_unlock(&g_mutex);

    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    long long expected = (long long)NUM_THREADS * INCREMENTS;
    printf("Expected: %lld\n", expected);
    printf("Got:      %lld\n", g_counter);
    int ok = g_counter == expected && !timed_out;
    printf("%s\n", timed_out ? "TIMED OUT" : (g_counter != expected ? "RACE DETECTED" : "OK"));

    pthread_cond_destroy(&g_done);
    return ok ? 0 : 1;
}
