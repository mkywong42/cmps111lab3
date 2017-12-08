#ifndef USERPROG_LAB3_H
#define USERPROG_LAB3_H

#include "threads/thread.h"
#include "threads/semaphore.h"
#include "threads/lock.h"
#include "threads/interrupt.h"

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

struct lock sys_lock;

void push_command(const char *cmdline UNUSED, void **esp);
void process_exit(void);
tid_t process_execute(const char *);
int process_wait(tid_t);

void write_handler(struct intr_frame *);
void exit_handler(struct intr_frame *);
void create_handler(struct intr_frame *);
void open_handler(struct intr_frame *);
void read_handler(struct intr_frame *);
void filesize_handler(struct intr_frame *);
void close_handler(struct intr_frame *);
void wait_handler(struct intr_frame *);
void exec_handler(struct intr_frame *);

#endif