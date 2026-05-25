#ifndef EDUOS_H
#define EDUOS_H

/*
 * eduos.h — Process Control Block + Process Manager declarations
 *
 * WINDOWS-COMPATIBLE VERSION
 * Uses Windows threads (windows.h) instead of pthreads.
 * No shm_open, no ucontext, no fork — all replaced with
 * Windows equivalents that compile with MinGW/GCC on Windows.
 */

#include <windows.h>
#include <stdarg.h>
#include <time.h>

#define MAX_PROCESSES      100
#define PROCESS_NAME_MAX   64
#define JSON_SNAPSHOT_FILE "pcb_snapshot.json"

/* ============================================================
   PROCESS STATES
   Every process is always in exactly one of these states.
============================================================ */
typedef enum {
    NEW,        /* Just created, not yet ready   */
    READY,      /* Waiting for the CPU            */
    RUNNING,    /* Currently using the CPU        */
    WAITING,    /* Blocked, waiting for something */
    TERMINATED  /* Finished running               */
} ProcessState;

/* ============================================================
   PROCESS CONTROL BLOCK (PCB)
   The OS keeps one of these for every process.
   Think of it as the process's identity card.
============================================================ */
typedef struct {
    int          pid;                     /* Unique process ID          */
    ProcessState state;                   /* Current state              */
    int          priority;                /* 0 = highest priority       */
    int          burst_time;              /* Total CPU time needed      */
    int          arrival_time;            /* When it arrived            */
    int          remaining_time;          /* CPU time still needed      */
    int          memory_req_kb;           /* Memory needed in KB        */
    int          thread_count;            /* Threads this process uses  */
    time_t       creation_time;           /* When it was created        */
    char         name[PROCESS_NAME_MAX];  /* Process name               */
    int          parent_pid;              /* PID of parent (-1 = init)  */
    int          exit_code;               /* 0 = success                */
} ProcessControlBlock;

/* ---- Function declarations ---- */
const char          *get_state_name(ProcessState state);
void                 log_message(const char *format, ...);
ProcessControlBlock *find_process(int pid);
void                 save_pcb_snapshot(void);
ProcessControlBlock *create_process(const char *name, int parent_pid,
                                    int priority, int burst_time,
                                    int memory_kb);
int                  edu_fork(ProcessControlBlock *parent);
void                 edu_exec(int pid, const char *prog_name);
int                  edu_wait(int parent_pid);
void                 edu_exit(int pid, int exit_code);
void                 edu_ps(void);

#endif /* EDUOS_H */
