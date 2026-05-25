#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define MAX_PROCESSES 100
#define PROCESS_NAME_MAX 64

/* ================================
   PROCESS STATES
================================ */
typedef enum {
    NEW,
    READY,
    RUNNING,
    WAITING,
    TERMINATED
} ProcessState;

/* ================================
   PROCESS CONTROL BLOCK
================================ */
typedef struct {
    int pid;                    /* unique process ID */
    ProcessState state;         /* NEW | READY | RUNNING | WAITING | TERMINATED */
    int priority;               /* 0 = highest */
    int burst_time;             /* total CPU time needed */
    int arrival_time;           /* clock tick of arrival */
    int remaining_time;         /* used by scheduling */
    int memory_req_kb;          /* memory footprint in KB */
    int thread_count;           /* threads spawned by process */
    time_t creation_time;       /* wall-clock timestamp */
    char name[PROCESS_NAME_MAX];/* process name */
    int parent_pid;             /* parent PID or -1 for init */
    int exit_code;              /* termination code */
} ProcessControlBlock;

/* ================================
   GLOBAL VARIABLES
================================ */
static ProcessControlBlock process_table[MAX_PROCESSES];
static int process_count = 0;
static int next_pid = 1000;

/* ================================
   HELPER FUNCTIONS
================================ */

const char* get_state_name(ProcessState state)
{
    switch (state)
    {
        case NEW: return "NEW";
        case READY: return "READY";
        case RUNNING: return "RUNNING";
        case WAITING: return "WAITING";
        case TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

void log_message(const char *format, ...)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char buffer[256];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    printf("[%02d:%02d:%02d] %s\n",
           t->tm_hour,
           t->tm_min,
           t->tm_sec,
           buffer);
}

ProcessControlBlock* find_process(int pid)
{
    for (int i = 0; i < process_count; i++)
    {
        if (process_table[i].pid == pid)
        {
            return &process_table[i];
        }
    }
    return NULL;
}

ProcessControlBlock* create_process(const char *name,
                                    int parent_pid,
                                    int priority,
                                    int burst_time,
                                    int memory_kb)
{
    if (process_count >= MAX_PROCESSES)
    {
        log_message("ERROR: Process table full");
        return NULL;
    }

    ProcessControlBlock *proc = &process_table[process_count];
    proc->pid = next_pid++;
    proc->state = READY;
    proc->priority = priority;
    proc->burst_time = burst_time;
    proc->remaining_time = burst_time;
    proc->arrival_time = 0;
    proc->memory_req_kb = memory_kb;
    proc->thread_count = 1;
    proc->creation_time = time(NULL);
    strncpy(proc->name, name, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    proc->parent_pid = parent_pid;
    proc->exit_code = 0;

    process_count++;
    log_message("Process created | PID=%d | Name=%s | Parent=%d",
                proc->pid,
                proc->name,
                proc->parent_pid);
    return proc;
}

/* ================================
   edu_fork()
================================ */

int edu_fork(ProcessControlBlock *parent)
{
    if (parent == NULL)
    {
        log_message("fork(): Invalid parent process pointer");
        return -1;
    }

    if (process_count >= MAX_PROCESSES)
    {
        log_message("fork(): Process table full");
        return -1;
    }

    ProcessControlBlock child = *parent;
    child.pid = next_pid++;
    child.parent_pid = parent->pid;
    child.state = NEW;
    child.creation_time = time(NULL);
    snprintf(child.name, PROCESS_NAME_MAX, "%s_child", parent->name);
    child.state = READY;
    child.remaining_time = child.burst_time;

    process_table[process_count++] = child;
    log_message("fork(): Parent PID=%d created Child PID=%d",
                parent->pid,
                child.pid);
    return child.pid;
}

/* ================================
   edu_exec()
================================ */

void edu_exec(int pid, const char *prog_name)
{
    ProcessControlBlock *proc = find_process(pid);
    if (proc == NULL)
    {
        log_message("exec(): Invalid PID=%d", pid);
        return;
    }

    strncpy(proc->name, prog_name, PROCESS_NAME_MAX - 1);
    proc->name[PROCESS_NAME_MAX - 1] = '\0';
    proc->burst_time = rand() % 20 + 5;
    proc->remaining_time = proc->burst_time;
    log_message("exec(): PID=%d replaced with '%s'",
                pid,
                proc->name);
}

/* ================================
   edu_wait()
================================ */

int edu_wait(int parent_pid)
{
    ProcessControlBlock *parent = find_process(parent_pid);
    if (parent == NULL)
    {
        log_message("wait(): Invalid parent PID=%d", parent_pid);
        return -1;
    }

    parent->state = WAITING;
    log_message("wait(): Parent PID=%d is waiting", parent_pid);

    int child_found = 0;
    for (int i = 0; i < process_count; i++)
    {
        if (process_table[i].parent_pid == parent_pid)
        {
            child_found = 1;
            if (process_table[i].state != TERMINATED)
            {
                log_message("wait(): Child PID=%d still running", process_table[i].pid);
                parent->state = READY;
                return -1;
            }
        }
    }

    if (!child_found)
    {
        log_message("wait(): Parent PID=%d has no children", parent_pid);
    }
    else
    {
        log_message("wait(): Parent PID=%d resumed after children terminated", parent_pid);
    }

    parent->state = READY;
    return 0;
}

/* ================================
   edu_exit()
================================ */

void edu_exit(int pid, int exit_code)
{
    ProcessControlBlock *proc = find_process(pid);
    if (proc == NULL)
    {
        log_message("exit(): Invalid PID=%d", pid);
        return;
    }

    proc->state = TERMINATED;
    proc->exit_code = exit_code;
    proc->remaining_time = 0;
    log_message("exit(): PID=%d terminated with exit code=%d",
                pid,
                exit_code);
}

void edu_ps(void)
{
    printf("\n============================================================\n");
    printf("%-6s %-15s %-12s %-8s %-8s %-8s %-8s\n",
           "PID",
           "NAME",
           "STATE",
           "PRIORITY",
           "BURST_TIME",
           "MEMORY",
           "PPID");
    printf("============================================================\n");

    for (int i = 0; i < process_count; i++)
    {
        ProcessControlBlock *p = &process_table[i];
        printf("%-6d %-15s %-12s %-8d %-8d %-8d %-8d\n",
               p->pid,
               p->name,
               get_state_name(p->state),
               p->priority,
               p->remaining_time,
               p->memory_req_kb,
               p->parent_pid);
    }

    printf("============================================================\n");
}

int main(void)
{
    srand((unsigned)time(NULL));

    ProcessControlBlock *init_proc = create_process("init", -1, 0, 15, 1024);
    if (init_proc == NULL)
    {
        return 1;
    }

    int child_pid = edu_fork(init_proc);
    if (child_pid > 0)
    {
        edu_exec(child_pid, "potplay.exe");
    }

    int child2_pid = edu_fork(init_proc);
    if (child2_pid > 0)
    {
        edu_exec(child2_pid, "spotify.exe");
    }

    edu_ps();

    edu_exit(child_pid, 0);
    edu_wait(init_proc->pid);
    edu_exit(child2_pid, 0);
    edu_ps();

    return 0;
}
