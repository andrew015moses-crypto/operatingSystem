#ifndef MANY_TO_ONE_H
#define MANY_TO_ONE_H

/*
 * many_to_one.h — Many-to-One cooperative scheduler
 * WINDOWS-COMPATIBLE: uses Windows Fibers instead of ucontext_t
 *
 * Windows Fibers are the Windows equivalent of POSIX ucontext.
 * They let us save and restore execution contexts manually,
 * which is exactly what a cooperative scheduler needs.
 */

void yield_thread(void);
void run_many_to_one(void (*fns[])(int id), int count);

#endif /* MANY_TO_ONE_H */
