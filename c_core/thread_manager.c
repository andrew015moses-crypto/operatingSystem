/*
 * thread_manager.c — Thread Pool
 * WINDOWS-COMPATIBLE VERSION
 *
 * Replaces pthreads with Windows threads:
 *   pthread_t          → HANDLE
 *   pthread_mutex_t    → CRITICAL_SECTION
 *   pthread_cond_t     → CONDITION_VARIABLE
 *   pthread_create()   → CreateThread()
 *   pthread_join()     → WaitForSingleObject()
 *   pthread_exit()     → ExitThread() / return
 *
 * HOW A THREAD POOL WORKS (restaurant analogy):
 *   task_queue    = the order ticket board
 *   worker threads = cooks waiting for tickets
 *   CRITICAL_SECTION = lock so two cooks don't grab the same ticket
 *   CONDITION_VARIABLE = a bell that wakes sleeping cooks
 */

#include "include/thread_manager.h"
#include <stdio.h>
#include <stdlib.h>

#define TASK_QUEUE_SIZE 16

typedef struct {
    ThreadTaskFn fn;
    void        *arg;
} ThreadTask;

static ThreadTask          task_queue[TASK_QUEUE_SIZE];
static int                 queue_head    = 0;
static int                 queue_tail    = 0;
static int                 queue_size    = 0;
static int                 pool_shutdown = 0;
static int                 pool_inited   = 0;
static int                 worker_count  = 0;

static CRITICAL_SECTION    queue_lock;
static CONDITION_VARIABLE  cond_not_empty;
static CONDITION_VARIABLE  cond_not_full;

static HANDLE worker_handles[THREAD_POOL_SIZE];

/* Worker thread — waits for a task, runs it, repeats*/
static DWORD WINAPI thread_pool_worker(LPVOID unused)
{
    (void)unused;
    for (;;)
    {
        EnterCriticalSection(&queue_lock);

        /* Wait while the queue is empty and pool is running */
        while (queue_size == 0 && !pool_shutdown)
            SleepConditionVariableCS(&cond_not_empty, &queue_lock, INFINITE);

        /* Shutdown signal with empty queue — exit */
        if (queue_size == 0 && pool_shutdown)
        {
            LeaveCriticalSection(&queue_lock);
            return 0;
        }

        /* Grab a task from the front of the queue */
        ThreadTask task = task_queue[queue_head];
        queue_head = (queue_head + 1) % TASK_QUEUE_SIZE;
        queue_size--;
        WakeConditionVariable(&cond_not_full);
        LeaveCriticalSection(&queue_lock);

        /* Run the task outside the lock */
        if (task.fn) task.fn(task.arg);
    }
}

/*  thread_pool_init — start the worker threads */
void thread_pool_init(void)
{
    if (pool_inited) return;
    InitializeCriticalSection(&queue_lock);
    InitializeConditionVariable(&cond_not_empty);
    InitializeConditionVariable(&cond_not_full);
    queue_head = queue_tail = queue_size = 0;
    pool_shutdown = 0;
    worker_count  = 0;
    pool_inited   = 1;

    for (int i = 0; i < THREAD_POOL_SIZE; ++i)
    {
        HANDLE h = CreateThread(NULL, 0, thread_pool_worker, NULL, 0, NULL);
        if (h == NULL)
        {
            fprintf(stderr, "thread_pool_init: CreateThread failed (%lu)\n",
                    GetLastError());
            break;
        }
        worker_handles[worker_count++] = h;
    }
}

/* thread_pool_submit — add a task to the queue */
int thread_pool_submit(ThreadTaskFn fn, void *arg)
{
    if (!fn || !pool_inited) return -1;
    EnterCriticalSection(&queue_lock);
    while (queue_size == TASK_QUEUE_SIZE && !pool_shutdown)
        SleepConditionVariableCS(&cond_not_full, &queue_lock, INFINITE);
    if (pool_shutdown) { LeaveCriticalSection(&queue_lock); return -1; }
    task_queue[queue_tail].fn  = fn;
    task_queue[queue_tail].arg = arg;
    queue_tail = (queue_tail + 1) % TASK_QUEUE_SIZE;
    queue_size++;
    WakeConditionVariable(&cond_not_empty);
    LeaveCriticalSection(&queue_lock);
    return 0;
}

/* thread_pool_shutdown — wait for all tasks to finish */
void thread_pool_shutdown(void)
{
    if (!pool_inited) return;
    EnterCriticalSection(&queue_lock);
    pool_shutdown = 1;
    WakeAllConditionVariable(&cond_not_empty);
    WakeAllConditionVariable(&cond_not_full);
    LeaveCriticalSection(&queue_lock);
    WaitForMultipleObjects(worker_count, worker_handles, TRUE, INFINITE);
    for (int i = 0; i < worker_count; ++i) CloseHandle(worker_handles[i]);
    DeleteCriticalSection(&queue_lock);
    pool_inited  = 0;
    worker_count = 0;
}
