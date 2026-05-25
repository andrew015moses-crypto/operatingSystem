#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

/*
 * thread_manager.h — Thread Pool declarations
 * WINDOWS-COMPATIBLE: uses Windows HANDLE threads
 */

#include <windows.h>

#define THREAD_POOL_SIZE 4

/* A task function: takes one void* argument */
typedef void (*ThreadTaskFn)(void *arg);

void thread_pool_init(void);
int  thread_pool_submit(ThreadTaskFn fn, void *arg);
void thread_pool_shutdown(void);

#endif /* THREAD_MANAGER_H */
