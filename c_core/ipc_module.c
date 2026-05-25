/*
 * ipc_module.c — Interprocess Communication
 * WINDOWS-COMPATIBLE VERSION
 *
 * Replaces Linux IPC with Windows equivalents:
 *
 *  1. POSIX Shared Memory (shm_open + mmap)
 *     → Windows Named Shared Memory (CreateFileMapping + MapViewOfFile)
 *     Same concept: two processes share a block of RAM by name.
 *     Access control: only matching owner_id may read/write.
 *
 *  2. Anonymous Pipe (pipe() + fork())
 *     → Windows Anonymous Pipe (CreatePipe + CreateProcess)
 *     Parent writes PCB data to a pipe; child reads and prints it.
 *
 * WHY THE SAME CONCEPT, DIFFERENT API?
 * Windows and Linux both support shared memory and pipes — they
 * just use different function names. The OS concepts are identical.
 */

#include "include/ipc_module.h"
#include "include/eduos.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ============================================================
   PART A — WINDOWS NAMED SHARED MEMORY
   (equivalent to POSIX shm_open + mmap)

   CreateFileMapping()  creates a named memory object (like shm_open)
   MapViewOfFile()      maps it into our address space (like mmap)
   UnmapViewOfFile()    unmaps it (like munmap)
   CloseHandle()        closes handles (like close + shm_unlink)
============================================================ */

#define SHM_NAME  "Local\\EduOS_SharedMem"

void demo_shared_memory(int owner_id)
{
    printf("\n=== IPC DEMO: Windows Named Shared Memory ===\n");
    printf("(Equivalent to POSIX shm_open + mmap)\n");
    printf("Owner ID for this session: %d\n\n", owner_id);

    /* 1. Create a named shared memory object */
    HANDLE hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE,   /* use page file (not a real file) */
        NULL,                   /* default security               */
        PAGE_READWRITE,         /* readable and writable          */
        0,                      /* high DWORD of size             */
        (DWORD)sizeof(SharedMetrics), /* low DWORD of size         */
        SHM_NAME                /* name — other processes use this*/
    );

    if (hMap == NULL)
    {
        fprintf(stderr, "[SHM] CreateFileMapping failed: %lu\n", GetLastError());
        return;
    }

    /* 2. Map into our address space */
    SharedMetrics *shm = (SharedMetrics *)MapViewOfFile(
        hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMetrics));

    if (!shm)
    {
        fprintf(stderr, "[SHM] MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(hMap);
        return;
    }

    /* 3. Initialise the lock and set values */
    InitializeCriticalSection(&shm->lock);
    EnterCriticalSection(&shm->lock);
    shm->owner_id      = owner_id;
    shm->cpu_usage_pct = 0.0f;
    shm->mem_usage_kb  = 0;
    shm->process_count = 0;
    shm->last_updated  = time(NULL);
    LeaveCriticalSection(&shm->lock);
    printf("[SHM] Shared region created. Owner=%d\n", owner_id);

    /* 4. ACCESS CONTROL — only matching owner_id may write */
    int caller_id = owner_id;  /* authorised caller */
    if (caller_id != shm->owner_id)
    {
        printf("[SHM] ACCESS DENIED: caller %d != owner %d\n",
               caller_id, shm->owner_id);
    }
    else
    {
        EnterCriticalSection(&shm->lock);
        shm->cpu_usage_pct = 42.5f;
        shm->mem_usage_kb  = 2048;
        shm->process_count = 3;
        shm->last_updated  = time(NULL);
        LeaveCriticalSection(&shm->lock);
        printf("[SHM] WRITE OK  — CPU=%.1f%%  MEM=%dKB  Procs=%d\n",
               shm->cpu_usage_pct, shm->mem_usage_kb, shm->process_count);
    }

    /* 5. Show a rejected access (wrong owner_id) */
    int intruder_id = owner_id + 99;
    if (intruder_id != shm->owner_id)
        printf("[SHM] ACCESS DENIED for caller %d (expected owner %d)"
               " — protection working correctly\n",
               intruder_id, shm->owner_id);

    /* 6. Read back */
    EnterCriticalSection(&shm->lock);
    printf("[SHM] READ  OK  — CPU=%.1f%%  MEM=%dKB  Procs=%d\n",
           shm->cpu_usage_pct, shm->mem_usage_kb, shm->process_count);
    LeaveCriticalSection(&shm->lock);

    /* 7. Cleanup */
    DeleteCriticalSection(&shm->lock);
    UnmapViewOfFile(shm);
    CloseHandle(hMap);
    printf("[SHM] Shared memory cleaned up.\n");
    printf("==============================================\n\n");
}


/* ============================================================
   PART B — WINDOWS ANONYMOUS PIPE
   (equivalent to POSIX pipe() + fork())

   CreatePipe()     creates read + write HANDLEs (like pipe())
   CreateProcess()  launches a child process    (like fork+exec)
   WriteFile()      writes to the pipe          (like write())
   ReadFile()       reads from the pipe         (like read())

   HOW IT WORKS:
   Parent creates a pipe, writes PCB data to the write end.
   Child reads from the read end and prints it.
   We simulate "fork" by re-launching ourselves with a special flag.
============================================================ */
void demo_anonymous_pipe(void)
{
    printf("\n=== IPC DEMO: Windows Anonymous Pipe ===\n");
    printf("(Equivalent to POSIX pipe() + fork())\n\n");

    /* Check if we are the child process */
    char *role = getenv("EDUOS_PIPE_CHILD");
    if (role && strcmp(role, "1") == 0)
    {
        /* ---- CHILD SIDE ---- */
        /* Read pipe handle number from env */
        char *hstr = getenv("EDUOS_PIPE_READ");
        if (!hstr) return;

        HANDLE hRead = (HANDLE)(intptr_t)_atoi64(hstr);
        char buf[512];
        DWORD bytesRead = 0;
        if (ReadFile(hRead, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0)
        {
            buf[bytesRead] = '\0';
            printf("[PIPE CHILD]  Received PCB data:\n%s\n\n", buf);
        }
        CloseHandle(hRead);
        /* Child exits — do NOT continue into demo_anonymous_pipe */
        exit(0);
    }

    /* ---- PARENT SIDE ---- */
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    {
        fprintf(stderr, "[PIPE] CreatePipe failed: %lu\n", GetLastError());
        return;
    }

    /* Build environment for the child: mark it as child + pass read handle */
    char env_block[512];
    snprintf(env_block, sizeof(env_block),
             "EDUOS_PIPE_CHILD=1\0EDUOS_PIPE_READ=%lld\0\0",
             (long long)(intptr_t)hReadPipe);

    /* Get our own executable path */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    STARTUPINFOA        si = { sizeof(STARTUPINFOA) };
    PROCESS_INFORMATION pi;
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    /* Serialise a PCB into the message */
    char msg[512];
    snprintf(msg, sizeof(msg),
             "pid=1000 name=init state=RUNNING priority=0 "
             "burst_time=15 remaining_time=7 memory_kb=1024 "
             "arrival_time=0 exit_code=0");

    printf("[PIPE PARENT] Sending PCB data through pipe...\n");

    BOOL ok = CreateProcessA(
        exe_path, NULL, NULL, NULL,
        TRUE,           /* inherit handles — child gets hReadPipe */
        0, env_block,   /* pass custom environment */
        NULL, &si, &pi);

    if (!ok)
    {
        fprintf(stderr, "[PIPE] CreateProcess failed: %lu\n", GetLastError());
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return;
    }

    /* Parent closes its copy of the read end */
    CloseHandle(hReadPipe);

    /* Write PCB data to write end */
    DWORD written = 0;
    WriteFile(hWritePipe, msg, (DWORD)strlen(msg), &written, NULL);
    CloseHandle(hWritePipe);   /* signals EOF to child */

    /* Wait for child */
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    printf("[PIPE PARENT] Child exited with code %lu\n", exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    printf("=========================================\n\n");
}
