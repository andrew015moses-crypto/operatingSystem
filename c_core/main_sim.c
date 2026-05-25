/*
 * main_sim.c — EduOS Main Entry Point
 * WINDOWS-COMPATIBLE VERSION
 *
 * Runs all demos in order:
 *   1. Process management  (edu_fork, edu_exec, edu_wait, edu_exit, edu_ps)
 *   2. Many-to-One threads (Windows Fibers cooperative scheduler)
 *   3. One-to-One threads  (Windows threads parallel sum)
 *   4. Thread pool         (worker queue with CRITICAL_SECTION)
 *   5. IPC — shared memory (Windows CreateFileMapping)
 *   6. IPC — anonymous pipe(Windows CreatePipe + CreateProcess)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "include/eduos.h"
#include "include/thread_manager.h"
#include "include/many_to_one.h"
#include "include/ipc_module.h"

/*
   ONE-TO-ONE THREADING MODEL DEMO
   Each user thread maps to a real Windows HANDLE thread.
   They run in parallel on multiple CPU cores.
*/
#define ONE_TO_ONE_THREADS 4

typedef struct {
    int       thread_id;
    int      *data;
    int       start;
    int       end;
    long long result;
} SumTaskArg;

static DWORD WINAPI sum_worker(LPVOID arg)
{
    SumTaskArg *task = (SumTaskArg *)arg;
    long long sum = 0;
    printf("[One-to-One] Thread %d summing [%d .. %d)\n",
           task->thread_id, task->start, task->end);
    for (int i = task->start; i < task->end; ++i)
        sum += task->data[i];
    task->result = sum;
    printf("[One-to-One] Thread %d done, partial sum = %lld\n",
           task->thread_id, sum);
    return 0;
}

static void simulate_one_to_one(void)
{
    printf("\n=== ONE-TO-ONE THREADING MODEL DEMO ===\n");
    printf("Each of %d threads maps to its own OS thread.\n", ONE_TO_ONE_THREADS);
    printf("They run in parallel on multiple CPU cores.\n\n");

    const int n = 400000;
    int *data = malloc(sizeof(int) * n);
    if (!data) { printf("[One-to-One] malloc failed\n"); return; }
    for (int i = 0; i < n; ++i) data[i] = 1;

    SumTaskArg args[ONE_TO_ONE_THREADS];
    HANDLE     handles[ONE_TO_ONE_THREADS];
    int        slice = n / ONE_TO_ONE_THREADS;

    for (int i = 0; i < ONE_TO_ONE_THREADS; ++i)
    {
        args[i].thread_id = i + 1;
        args[i].data      = data;
        args[i].start     = i * slice;
        args[i].end       = (i == ONE_TO_ONE_THREADS-1) ? n : args[i].start + slice;
        args[i].result    = 0;
        handles[i] = CreateThread(NULL, 0, sum_worker, &args[i], 0, NULL);
        if (!handles[i])
        {
            printf("[One-to-One] CreateThread failed for thread %d\n", i+1);
            handles[i] = NULL;
        }
    }

    long long total = 0;
    for (int i = 0; i < ONE_TO_ONE_THREADS; ++i)
    {
        if (handles[i])
        {
            WaitForSingleObject(handles[i], INFINITE);
            CloseHandle(handles[i]);
        }
        total += args[i].result;
    }
    printf("\n[One-to-One] Final sum = %lld (expected %d)\n", total, n);
    printf("========================================\n\n");
    free(data);
}

/*
   MANY-TO-ONE USER THREAD FUNCTIONS
   Each calls yield_thread() to cooperatively give up the CPU.
*/
static void user_thread_A(int id)
{
    for (int step = 1; step <= 3; ++step)
    {
        printf("[Many-to-One] Thread %d (A) step %d\n", id, step);
        yield_thread();
    }
}

static void user_thread_B(int id)
{
    for (int step = 1; step <= 3; ++step)
    {
        printf("[Many-to-One] Thread %d (B) step %d\n", id, step);
        yield_thread();
    }
}

static void user_thread_C(int id)
{
    for (int step = 1; step <= 2; ++step)
    {
        printf("[Many-to-One] Thread %d (C) step %d\n", id, step);
        yield_thread();
    }
}

/*THREAD POOL DEMO*/
static void thread_pool_task(void *arg)
{
    int task_id = *(int *)arg;
    log_message("Thread pool task %d started", task_id);
    /* Simulate work */
    for (volatile int i = 0; i < 5000000; ++i) {}
    log_message("Thread pool task %d finished", task_id);
}

static void demo_thread_pool(void)
{
    printf("\n=== THREAD POOL DEMO ===\n");
    printf("Submitting %d tasks to a pool of %d workers.\n\n",
           THREAD_POOL_SIZE * 2, THREAD_POOL_SIZE);
    thread_pool_init();
    static int task_ids[THREAD_POOL_SIZE * 2];
    for (int i = 0; i < THREAD_POOL_SIZE * 2; ++i)
    {
        task_ids[i] = i + 1;
        thread_pool_submit(thread_pool_task, &task_ids[i]);
    }
    thread_pool_shutdown();
    printf("========================\n\n");
}

/*MAIN*/
int main(void)
{
    /* Check if we are the child process spawned by demo_anonymous_pipe */
    char *role = getenv("EDUOS_PIPE_CHILD");
    if (role && strcmp(role, "1") == 0)
    {
        demo_anonymous_pipe();   /* runs child-side code and exits */
        return 0;
    }

    srand((unsigned)time(NULL));

    printf("============================================================\n");
    printf("  EduOS — Educational Operating System Simulator\n");
    printf("  Windows-Compatible Build\n");
    printf("============================================================\n\n");

    /* 1. Process management */
    printf("--- Process Management ---\n");
    ProcessControlBlock *init_proc = create_process("init", -1, 0, 15, 1024);
    if (!init_proc) return 1;

    int child_pid  = edu_fork(init_proc);
    if (child_pid  > 0) edu_exec(child_pid,  "chrome.exe");
    int child2_pid = edu_fork(init_proc);
    if (child2_pid > 0) edu_exec(child2_pid, "spotify.exe");

    edu_ps();
    edu_exit(child_pid,  0);
    edu_wait(init_proc->pid);
    edu_exit(child2_pid, 0);
    edu_wait(init_proc->pid);
    edu_ps();

    /* 2. Many-to-One (Windows Fibers) */
    void (*fns[])(int) = { user_thread_A, user_thread_B, user_thread_C };
    run_many_to_one(fns, 3);

    /* 3. One-to-One (Windows Threads) */
    simulate_one_to_one();

    /* 4. Thread pool */
    demo_thread_pool();

    /* 5. IPC — shared memory */
    demo_shared_memory(42);

    /* 6. IPC — anonymous pipe */
    demo_anonymous_pipe();

    printf("EduOS simulation complete.\n");
    return 0;
}
