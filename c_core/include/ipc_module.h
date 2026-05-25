#ifndef IPC_MODULE_H
#define IPC_MODULE_H

/*
 * ipc_module.h — IPC declarations
 * WINDOWS-COMPATIBLE VERSION
 *
 * Replaces:
 *   shm_open/mmap  → Windows Named Shared Memory (CreateFileMapping)
 *   pipe/fork      → Windows Anonymous Pipe (CreatePipe + CreateProcess)
 */

#include <windows.h>
#include <time.h>

/* Shared memory region layout.
   owner_id is the access-control key — callers must match it. */
typedef struct {
    CRITICAL_SECTION lock;       /* Windows mutex equivalent    */
    int              owner_id;   /* Only this owner may access  */
    float            cpu_usage_pct;
    int              mem_usage_kb;
    int              process_count;
    time_t           last_updated;
} SharedMetrics;

void demo_shared_memory(int owner_id);
void demo_anonymous_pipe(void);

#endif /* IPC_MODULE_H */
