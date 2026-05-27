/*
 * race_demo.c — Race Condition Demonstration
 * WINDOWS-COMPATIBLE VERSION
 *
 * Compile targets (see build.bat):
 *   build_race.bat  → WITHOUT mutex: counter is wrong every time
 *   build_fixed.bat → WITH    mutex: counter is always 400000
 *
 * WHAT IS A RACE CONDITION?
 * -------------------------
 * Thread A reads counter = 5
 * Thread B reads counter = 5   <-- B reads BEFORE A writes back
 * Thread A writes counter = 6
 * Thread B writes counter = 6  <-- B's increment is LOST
 * Expected = 7, Actual = 6.  That lost update is a race condition.
 *
 * DEADLOCK EXPLANATION:
 * Thread X locks mutex_A then tries mutex_B.
 * Thread Y locks mutex_B then tries mutex_A.
 * Both wait forever — DEADLOCK.
 * Fix: always lock in the SAME order (A then B).
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================
   SHARED COUNTER
============================================================ */
#define NUM_THREADS  4
#define INCREMENTS   100000

static long              shared_counter = 0;

/* Only declare the lock when it is actually used (FIXED build).
 * In UNSAFE_BUILD the lock is intentionally absent — declaring it
 * unused would trigger -Wunused-variable, so we guard it here. */
#ifndef UNSAFE_BUILD
static CRITICAL_SECTION  counter_lock;
#endif

/* ============================================================
   WORKER THREAD
   UNSAFE_BUILD = no lock  → race condition
   (no define)  = lock     → always correct
============================================================ */
static DWORD WINAPI counter_worker(LPVOID arg)
{
    (void)arg;
    for (int i = 0; i < INCREMENTS; ++i)
    {
#ifdef UNSAFE_BUILD
        /* NO LOCK — race condition: threads overwrite each other */
        shared_counter++;
#else
        /* WITH LOCK — safe */
        EnterCriticalSection(&counter_lock);
        shared_counter++;
        LeaveCriticalSection(&counter_lock);
#endif
    }
    return 0;
}

/* ============================================================
   SEMAPHORE PRODUCER-CONSUMER
   Windows semaphores: CreateSemaphore / WaitForSingleObject / ReleaseSemaphore
============================================================ */
#define BUFFER_SIZE      5
#define ITEMS_TO_PRODUCE 12

static int     buffer[BUFFER_SIZE];
static int     buf_in  = 0;
static int     buf_out = 0;
static HANDLE  sem_empty;   /* counts empty slots  */
static HANDLE  sem_filled;  /* counts filled slots */
static CRITICAL_SECTION buf_lock;

static DWORD WINAPI producer(LPVOID arg)
{
    (void)arg;
    for (int item = 1; item <= ITEMS_TO_PRODUCE; ++item)
    {
        WaitForSingleObject(sem_empty, INFINITE);   /* wait for empty slot */
        EnterCriticalSection(&buf_lock);
        buffer[buf_in] = item;
        buf_in = (buf_in + 1) % BUFFER_SIZE;
        printf("[Producer] produced item %d\n", item);
        LeaveCriticalSection(&buf_lock);
        ReleaseSemaphore(sem_filled, 1, NULL);       /* signal filled slot */
    }
    return 0;
}

static DWORD WINAPI consumer(LPVOID arg)
{
    (void)arg;
    for (int i = 0; i < ITEMS_TO_PRODUCE; ++i)
    {
        WaitForSingleObject(sem_filled, INFINITE);  /* wait for item */
        EnterCriticalSection(&buf_lock);
        int item = buffer[buf_out];
        buf_out = (buf_out + 1) % BUFFER_SIZE;
        printf("[Consumer] consumed item %d\n", item);
        LeaveCriticalSection(&buf_lock);
        ReleaseSemaphore(sem_empty, 1, NULL);        /* signal empty slot */
    }
    return 0;
}

static void demo_producer_consumer(void)
{
    printf("\n=== SEMAPHORE PRODUCER-CONSUMER DEMO ===\n");
    InitializeCriticalSection(&buf_lock);
    sem_empty  = CreateSemaphore(NULL, BUFFER_SIZE, BUFFER_SIZE, NULL);
    sem_filled = CreateSemaphore(NULL, 0,           BUFFER_SIZE, NULL);

    HANDLE prod = CreateThread(NULL, 0, producer, NULL, 0, NULL);
    HANDLE cons = CreateThread(NULL, 0, consumer, NULL, 0, NULL);
    HANDLE both[2] = {prod, cons};
    WaitForMultipleObjects(2, both, TRUE, INFINITE);
    CloseHandle(prod); CloseHandle(cons);
    CloseHandle(sem_empty); CloseHandle(sem_filled);
    DeleteCriticalSection(&buf_lock);
    printf("[Sem] Producer-consumer finished cleanly.\n");
    printf("=========================================\n\n");
}

/* ============================================================
   DEADLOCK FIX DEMO
   Both threads lock in the same order (A then B).
   No deadlock possible.
============================================================ */
#ifndef UNSAFE_BUILD
static CRITICAL_SECTION mutex_A;
static CRITICAL_SECTION mutex_B;

static DWORD WINAPI fixed_thread_x(LPVOID arg)
{
    (void)arg;
    EnterCriticalSection(&mutex_A);
    printf("[Fixed]  Thread X locked A, trying B...\n");
    Sleep(1);
    EnterCriticalSection(&mutex_B);
    printf("[Fixed]  Thread X got both locks (A then B)\n");
    LeaveCriticalSection(&mutex_B);
    LeaveCriticalSection(&mutex_A);
    return 0;
}

static DWORD WINAPI fixed_thread_y(LPVOID arg)
{
    (void)arg;
    /* FIXED: also locks A FIRST — consistent order prevents deadlock */
    EnterCriticalSection(&mutex_A);
    printf("[Fixed]  Thread Y locked A, trying B...\n");
    Sleep(1);
    EnterCriticalSection(&mutex_B);
    printf("[Fixed]  Thread Y got both locks (A then B)\n");
    LeaveCriticalSection(&mutex_B);
    LeaveCriticalSection(&mutex_A);
    return 0;
}
#endif

/* ============================================================
   MAIN
============================================================ */
int main(void)
{
#ifdef UNSAFE_BUILD
    printf("============================================================\n");
    printf("  RACE CONDITION DEMO  (UNSAFE — no mutex)\n");
    printf("  Expected counter = %d\n", NUM_THREADS * INCREMENTS);
    printf("  Actual will be LESS due to lost updates.\n");
    printf("============================================================\n\n");
#else
    printf("============================================================\n");
    printf("  RACE CONDITION DEMO  (FIXED — mutex protected)\n");
    printf("  Expected counter = %d\n", NUM_THREADS * INCREMENTS);
    printf("  Actual should EXACTLY match expected.\n");
    printf("============================================================\n\n");
    InitializeCriticalSection(&counter_lock);
#endif

    /* Counter race demo */
    HANDLE threads[NUM_THREADS];
    shared_counter = 0;
    for (int i = 0; i < NUM_THREADS; ++i)
        threads[i] = CreateThread(NULL, 0, counter_worker, NULL, 0, NULL);
    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, INFINITE);
    for (int i = 0; i < NUM_THREADS; ++i) CloseHandle(threads[i]);

    printf("Expected  : %d\n", NUM_THREADS * INCREMENTS);
    printf("Actual    : %ld\n", shared_counter);
    printf("Difference: %ld  %s\n\n",
           (long)(NUM_THREADS * INCREMENTS) - shared_counter,
           shared_counter == (long)(NUM_THREADS * INCREMENTS)
               ? "(CORRECT)" : "(DATA RACE — lost updates!)");

    /* Producer-consumer with semaphores */
    demo_producer_consumer();

#ifndef UNSAFE_BUILD
    /* Deadlock fix demo */
    printf("=== DEADLOCK FIX: consistent lock order (A then B) ===\n");
    InitializeCriticalSection(&mutex_A);
    InitializeCriticalSection(&mutex_B);
    HANDLE tx = CreateThread(NULL, 0, fixed_thread_x, NULL, 0, NULL);
    HANDLE ty = CreateThread(NULL, 0, fixed_thread_y, NULL, 0, NULL);
    HANDLE pair[2] = {tx, ty};
    WaitForMultipleObjects(2, pair, TRUE, INFINITE);
    CloseHandle(tx); CloseHandle(ty);
    DeleteCriticalSection(&mutex_A);
    DeleteCriticalSection(&mutex_B);
    printf("[Deadlock fixed] Both threads finished without hanging.\n");
    printf("======================================================\n\n");
    DeleteCriticalSection(&counter_lock);
#endif

    return 0;
}
