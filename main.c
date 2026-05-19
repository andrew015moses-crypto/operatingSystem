
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PROCESSES 100

typedef int pid_t;

typedef enum
{
    NEW,
    READY,
    RUNNING,
    WAITING,
    TERMINATED
} ProcessState;

typedef struct
{
    int process_id;
    int parent_process_id;
    char program_name[64];
    ProcessState current_state;
    int priority_level;
    int arrival_time;
    int burst_time;
    int remaining_time;
    int memory_required_kb;
    time_t creation_timestamp;
    int thread_count;
    int exit_code;
} ProcessControlBlock;


/* Function to be used for behaviour of each stage of a process */
void create_process(ProcessControlBlock *parent_process, const char *program_name);
pid_t edu_fork(ProcessControlBlock *parent_process);
void execute_process(int process_id, const char *new_program_name);
int wait_for_process(int parent_process_id);
int terminate_process(int process_id, int exit_code);
void print_process_table(void);

ProcessControlBlock process_table[MAX_PROCESSES];
int next_process_id = 1;
int total_processes = 0;

void create_process(ProcessControlBlock *parent_process, const char *program_name)
{
    if (total_processes >= MAX_PROCESSES)
    {
        printf("Process table is full.\n");
        return;
    }

    ProcessControlBlock *new_process = &process_table[total_processes];
    new_process->process_id = next_process_id++;
    new_process->parent_process_id = parent_process ? parent_process->process_id : 0;
    strcpy(new_process->program_name, program_name);
    new_process->current_state = READY;
    new_process->priority_level = 1;    
    new_process->arrival_time = 0;
    new_process->burst_time = 10;       
    new_process->remaining_time = 10;
    new_process->memory_required_kb = 1024;
    new_process->creation_timestamp = time(NULL);
    new_process->thread_count = 1;
    new_process->exit_code = 0;

    total_processes++;

    printf("[%ld] Process created | PID=%d | Program=%s\n",
           (long)time(NULL), new_process->process_id, new_process->program_name);
}

void execute_process(int process_id, const char *new_program_name)
{
    for (int i = 0; i < total_processes; ++i)
    {
        if (process_table[i].process_id == process_id)
        {
            process_table[i].current_state = RUNNING;
            strcpy(process_table[i].program_name, new_program_name);
            printf("[%ld] Executing PID=%d -> %s\n",
                   (long)time(NULL), process_id, process_table[i].program_name);
            return;
        }
    }

    printf("Process %d not found.\n", process_id);
}


 