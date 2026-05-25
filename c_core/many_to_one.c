
#include "include/many_to_one.h"
#include "include/eduos.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

#define MAX_USER_THREADS 8

typedef struct {
    LPVOID  fiber;       /* Windows fiber handle */
    int     id;
    int     finished;
    void  (*fn)(int id);
} UserThread;

static UserThread ut[MAX_USER_THREADS];
static int        ut_count      = 0;
static int        current_ut    = -1;
static LPVOID     scheduler_fiber = NULL;

/* yield_thread — give up CPU, return to scheduler*/
void yield_thread(void)
{
    if (current_ut < 0 || !scheduler_fiber) return;
    current_ut = -1;
    SwitchToFiber(scheduler_fiber);
}

/*fiber_entry — called when a fiber starts*/
static void WINAPI fiber_entry(LPVOID param)
{
    int idx = (int)(intptr_t)param;
    if (idx >= 0 && idx < ut_count)
    {
        current_ut = idx;
        ut[idx].fn(ut[idx].id);
    }
    ut[idx].finished = 1;
    printf("[Many-to-One] Thread %d finished\n", ut[idx].id);
    SwitchToFiber(scheduler_fiber);   /* return to scheduler */
}

/*run_many_to_one — register and run user threads*/
void run_many_to_one(void (*fns[])(int id), int count)
{
    ut_count   = 0;
    current_ut = -1;

    printf("\n=== MANY-TO-ONE THREADING MODEL DEMO ===\n");
    printf("All %d user threads share ONE kernel thread.\n", count);
    printf("If any thread blocks, ALL threads are frozen.\n\n");

    if (count > MAX_USER_THREADS) count = MAX_USER_THREADS;

    /* Convert the current thread into a fiber (required before SwitchToFiber) */
    scheduler_fiber = ConvertThreadToFiber(NULL);
    if (!scheduler_fiber)
    {
        /* Already a fiber — get current fiber instead */
        scheduler_fiber = GetCurrentFiber();
    }

    /* Create a fiber for each user thread */
    for (int i = 0; i < count; ++i)
    {
        ut[i].id       = i + 1;
        ut[i].fn       = fns[i];
        ut[i].finished = 0;
        /* 64 KB stack, entry function, pass index as parameter */
        ut[i].fiber = CreateFiber(65536, fiber_entry, (LPVOID)(intptr_t)i);
        if (!ut[i].fiber)
        {
            fprintf(stderr, "[Many-to-One] CreateFiber failed for thread %d\n", i+1);
            ut[i].finished = 1;
        }
        ut_count++;
        printf("[Many-to-One] User thread %d registered\n", ut[i].id);
    }

    printf("[Many-to-One] Scheduler starting with %d threads\n\n", ut_count);

    /* Round-robin scheduler loop */
    int all_done = 0;
    while (!all_done)
    {
        all_done = 1;
        for (int i = 0; i < ut_count; ++i)
        {
            if (ut[i].finished || !ut[i].fiber) continue;
            all_done   = 0;
            current_ut = i;
            SwitchToFiber(ut[i].fiber);
            /* Returns here when fiber yields or finishes */
        }
    }

    /* Cleanup fibers */
    for (int i = 0; i < ut_count; ++i)
        if (ut[i].fiber) DeleteFiber(ut[i].fiber);

    printf("[Many-to-One] All user threads completed\n");
    current_ut = -1;
    printf("=========================================\n\n");
}
