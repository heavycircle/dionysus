#include <dino/core/thread.h>

#ifdef _WIN32

    #include <windows.h>

int dino_thread_create(dino_thread_t *thread, dino_thread_worker_t worker, void *arg)
{
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)worker, arg, 0, NULL);
    if (hThread == NULL)
        return 1;

    *thread = (dino_thread_t)hThread;
    return 0;
}

void dino_thread_join(dino_thread_t thread)
{
    HANDLE hThread = (HANDLE)thread;

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
}

#else

    #include <pthread.h>

int dino_thread_create(dino_thread_t *thread, dino_thread_worker_t worker, void *arg)
{
    pthread_t pt;

    int result = pthread_create(&pt, NULL, (void *(*)(void *))worker, arg);
    if (result == 0)
        *thread = (dino_thread_t)pt;

    return result;
}

void dino_thread_join(dino_thread_t thread)
{
    pthread_t pt = (pthread_t)thread;

    pthread_join(pt, NULL);
}

#endif // _WIN32
