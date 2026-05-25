/*
 * process_manager.c  —  Process Control Block & System Calls
 * WINDOWS-COMPATIBLE VERSION
 *
 * Uses CRITICAL_SECTION (Windows) instead of pthread_mutex_t.
 * Everything else (PCB logic, JSON writer) is identical.
 */

#include "include/eduos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- Shared state ---- */
static ProcessControlBlock process_table[MAX_PROCESSES];
static CRITICAL_SECTION    process_table_lock;
static int process_count  = 0;
static int next_pid       = 1000;
static int manager_init   = 0;

/* Initialise the lock once */
static void ensure_init(void)
{
    if (!manager_init)
    {
        InitializeCriticalSection(&process_table_lock);
        manager_init = 1;
    }
}

/* get_state_name — converts enum to string*/
const char *get_state_name(ProcessState state)
{
    switch (state)
    {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case RUNNING:    return "RUNNING";
        case WAITING:    return "WAITING";
        case TERMINATED: return "TERMINATED";
        default:         return "UNKNOWN";
    }
}

/* 
   log_message — timestamped console output
   Example: [14:32:05] Process created | PID=1000
*/
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
           t->tm_hour, t->tm_min, t->tm_sec, buffer);
}

/* find_process — search table by PID*/
ProcessControlBlock *find_process(int pid)
{
    ensure_init();
    EnterCriticalSection(&process_table_lock);
    ProcessControlBlock *result = NULL;
    for (int i = 0; i < process_count; ++i)
        if (process_table[i].pid == pid)
            { result = &process_table[i]; break; }
    LeaveCriticalSection(&process_table_lock);
    return result;
}

/*json_escape_string — hand-written, no library*/
static void json_escape_string(const char *src, char *dst, size_t sz)
{
    size_t j = 0;
    if (sz == 0) return;
    for (size_t i = 0; src[i] && j + 1 < sz; ++i)
    {
        char c = src[i];
        if      (c == '"'  || c == '\\') { if (j+2<sz){dst[j++]='\\';dst[j++]=c;} }
        else if (c == '\n')              { if (j+2<sz){dst[j++]='\\';dst[j++]='n';} }
        else if (c == '\r')              { if (j+2<sz){dst[j++]='\\';dst[j++]='r';} }
        else if (c == '\t')              { if (j+2<sz){dst[j++]='\\';dst[j++]='t';} }
        else                             { dst[j++] = c; }
    }
    dst[j] = '\0';
}

/* 
   save_pcb_snapshot — writes pcb_snapshot.json
   Called every time a process changes state.
   The Python scheduler (Part 3) reads this file.
*/
void save_pcb_snapshot(void)
{
    FILE *fp = fopen(JSON_SNAPSHOT_FILE, "w");
    if (!fp) { log_message("ERROR: Cannot open %s", JSON_SNAPSHOT_FILE); return; }

    EnterCriticalSection(&process_table_lock);
    fprintf(fp, "{\n");
    fprintf(fp, "  \"timestamp\": %lld,\n", (long long)time(NULL));
    fprintf(fp, "  \"process_count\": %d,\n", process_count);
    fprintf(fp, "  \"processes\": [\n");
    for (int i = 0; i < process_count; ++i)
    {
        ProcessControlBlock *p = &process_table[i];
        char esc[PROCESS_NAME_MAX * 2];
        json_escape_string(p->name, esc, sizeof(esc));
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"pid\": %d,\n",            p->pid);
        fprintf(fp, "      \"ppid\": %d,\n",           p->parent_pid);
        fprintf(fp, "      \"name\": \"%s\",\n",       esc);
        fprintf(fp, "      \"state\": \"%s\",\n",      get_state_name(p->state));
        fprintf(fp, "      \"priority\": %d,\n",       p->priority);
        fprintf(fp, "      \"burst_time\": %d,\n",     p->burst_time);
        fprintf(fp, "      \"arrival_time\": %d,\n",   p->arrival_time);
        fprintf(fp, "      \"remaining_time\": %d,\n", p->remaining_time);
        fprintf(fp, "      \"memory_kb\": %d,\n",      p->memory_req_kb);
        fprintf(fp, "      \"thread_count\": %d,\n",   p->thread_count);
        fprintf(fp, "      \"creation_time\": %lld,\n",(long long)p->creation_time);
        fprintf(fp, "      \"exit_code\": %d\n",       p->exit_code);
        fprintf(fp, "    }%s\n", (i < process_count-1) ? "," : "");
    }
    fprintf(fp, "  ]\n}\n");
    LeaveCriticalSection(&process_table_lock);
    fclose(fp);
}

static void snapshot_state(const char *event)
{
    save_pcb_snapshot();
    log_message("PCB snapshot saved after: %s", event);
}

/*create_process*/
ProcessControlBlock *create_process(const char *name, int parent_pid,
                                     int priority, int burst_time,
                                     int memory_kb)
{
    ensure_init();
    EnterCriticalSection(&process_table_lock);
    if (process_count >= MAX_PROCESSES)
    {
        LeaveCriticalSection(&process_table_lock);
        log_message("ERROR: Process table full");
        return NULL;
    }
    ProcessControlBlock *p = &process_table[process_count];
    p->pid            = next_pid++;
    p->state          = READY;
    p->priority       = priority;
    p->burst_time     = burst_time;
    p->arrival_time   = 0;
    p->remaining_time = burst_time;
    p->memory_req_kb  = memory_kb;
    p->thread_count   = 1;
    p->creation_time  = time(NULL);
    p->parent_pid     = parent_pid;
    p->exit_code      = 0;
    strncpy(p->name, name, PROCESS_NAME_MAX - 1);
    p->name[PROCESS_NAME_MAX - 1] = '\0';
    process_count++;
    LeaveCriticalSection(&process_table_lock);
    log_message("Process created | PID=%d | Name=%s | Parent=%d",
                p->pid, p->name, p->parent_pid);
    snapshot_state("create_process");
    return p;
}

/* 
   edu_fork — creates a child copy of parent
   Simulates UNIX fork(): child inherits parent data, gets new PID
*/
int edu_fork(ProcessControlBlock *parent)
{
    if (!parent) { log_message("fork(): NULL parent"); return -1; }
    ensure_init();
    EnterCriticalSection(&process_table_lock);
    if (process_count >= MAX_PROCESSES)
    {
        LeaveCriticalSection(&process_table_lock);
        log_message("fork(): Table full");
        return -1;
    }
    ProcessControlBlock child  = *parent;
    child.pid            = next_pid++;
    child.parent_pid     = parent->pid;
    child.state          = READY;
    child.creation_time  = time(NULL);
    child.remaining_time = child.burst_time;
    snprintf(child.name, PROCESS_NAME_MAX, "%.56s_child", parent->name);
    process_table[process_count++] = child;
    LeaveCriticalSection(&process_table_lock);
    log_message("fork(): PID=%d created child PID=%d", parent->pid, child.pid);
    snapshot_state("edu_fork");
    return child.pid;
}

/* 
   edu_exec — replaces process program image
   Simulates UNIX execve(): new program name, reset burst time
 */
void edu_exec(int pid, const char *prog_name)
{
    ProcessControlBlock *p = find_process(pid);
    if (!p) { log_message("exec(): Invalid PID=%d", pid); return; }
    EnterCriticalSection(&process_table_lock);
    strncpy(p->name, prog_name, PROCESS_NAME_MAX - 1);
    p->name[PROCESS_NAME_MAX - 1] = '\0';
    p->burst_time     = rand() % 20 + 5;
    p->remaining_time = p->burst_time;
    LeaveCriticalSection(&process_table_lock);
    log_message("exec(): PID=%d is now '%s'", pid, p->name);
    snapshot_state("edu_exec");
}

/*
   edu_wait — parent waits for all children to terminate
   Simulates UNIX wait()
*/
int edu_wait(int parent_pid)
{
    ProcessControlBlock *parent = find_process(parent_pid);
    if (!parent) { log_message("wait(): Invalid PID=%d", parent_pid); return -1; }
    EnterCriticalSection(&process_table_lock);
    parent->state = WAITING;
    LeaveCriticalSection(&process_table_lock);
    log_message("wait(): PID=%d now WAITING", parent_pid);
    snapshot_state("edu_wait_waiting");
    for (int i = 0; i < process_count; i++)
    {
        if (process_table[i].parent_pid == parent_pid &&
            process_table[i].state != TERMINATED)
        {
            log_message("wait(): Child PID=%d still running",
                        process_table[i].pid);
            EnterCriticalSection(&process_table_lock);
            parent->state = READY;
            LeaveCriticalSection(&process_table_lock);
            snapshot_state("edu_wait_ready");
            return -1;
        }
    }
    EnterCriticalSection(&process_table_lock);
    parent->state = READY;
    LeaveCriticalSection(&process_table_lock);
    log_message("wait(): PID=%d resumed", parent_pid);
    snapshot_state("edu_wait_complete");
    return 0;
}

/*
   edu_exit — terminates a process
   Simulates UNIX _exit()
 */
void edu_exit(int pid, int exit_code)
{
    ProcessControlBlock *p = find_process(pid);
    if (!p) { log_message("exit(): Invalid PID=%d", pid); return; }
    EnterCriticalSection(&process_table_lock);
    p->state          = TERMINATED;
    p->exit_code      = exit_code;
    p->remaining_time = 0;
    LeaveCriticalSection(&process_table_lock);
    log_message("exit(): PID=%d terminated (code=%d)", pid, exit_code);
    snapshot_state("edu_exit");
}

/*
   edu_ps — prints the process table (like Linux ps aux)
*/
void edu_ps(void)
{
    printf("\n============================================================\n");
    printf("%-6s %-15s %-12s %-8s %-10s %-8s %-8s\n",
           "PID","NAME","STATE","PRIORITY","REMAINING","MEM_KB","PPID");
    printf("============================================================\n");
    EnterCriticalSection(&process_table_lock);
    for (int i = 0; i < process_count; i++)
    {
        ProcessControlBlock *p = &process_table[i];
        printf("%-6d %-15s %-12s %-8d %-10d %-8d %-8d\n",
               p->pid, p->name, get_state_name(p->state),
               p->priority, p->remaining_time,
               p->memory_req_kb, p->parent_pid);
    }
    LeaveCriticalSection(&process_table_lock);
    printf("============================================================\n\n");
}
