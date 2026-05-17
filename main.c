
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