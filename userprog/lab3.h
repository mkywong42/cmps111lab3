#ifndef USERPROG_LAB3_H
#define USERPROG_LAB3_H

#include "threads/thread.h"
#include "threads/semaphore.h"

struct process_block {
    int pid;
    const char* cmdline;
    struct semaphore init_child;
    struct semaphore waiter;
    struct list_elem elem;
    bool finished;
    int exit_code;
};

struct file_info {
    int id;
    struct list_elem elem;
    struct file *filename;
};

void
push_command(const char *cmdline UNUSED, void **esp);

// void
// start_process(void *cmdline);

void process_exit(void);
tid_t process_execute(const char *);
int process_wait(tid_t);
#endif