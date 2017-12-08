
#include "lab3.h"

#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "devices/timer.h"
#include "userprog/umem.h"

static thread_func start_process NO_RETURN;

/*
 * Push the command and arguments found in CMDLINE onto the stack, world 
 * aligned with the stack pointer ESP. Should only be called after the ELF 
 * format binary has been loaded into the heap by load();
 */
void
push_command(const char *cmdline UNUSED, void **esp)
{
    // printf("Base Address: 0x%08x\n", *esp);

    uint32_t argc = 0;
    const char **argv = (const char**) palloc_get_page(0);

    // Parse commandline and divide up the string into arguments
    // Finds number of arguments (argc) and makes array of arguments (argv)
    char* pointer;
    char* commandline = cmdline;
    char* argument = strtok_r(commandline, " ", &pointer);
    for(; argument != NULL; argument = strtok_r(NULL, " ", &pointer)){
        argv[argc] = argument;
        argc++;
    }

    void* arg_addr[argc];

    // Add the commandline to the stack starting from rightmost argument
    for(int i = argc - 1; i >= 0; i--){
        int arg_len = strlen(argv[i])+1;
        *esp -= arg_len;
        memcpy(*esp, argv[i], arg_len);
        arg_addr[i] = *esp;
    }

    // Word align with the stack pointer. DO NOT REMOVE THIS LINE.
    *esp = (void*) ((unsigned int) (*esp) & 0xfffffffc);

    // Add the terminating NULL to the stack
    *esp -= 4;
    *((uint32_t*) *esp) = 0;

    // Add the addresses of the arguments on the stack to the stack
    for(int j = argc - 1; j > -1; j--){
        *esp -= 4;
        *((void**)*esp) = arg_addr[j];
    }

    // Adds the address of the beginning of argv to the stack
    *esp -= 4;
    *((void**)*esp) = (*esp + 4);

    // Adds argc to the stack
    *esp -= 4;
    *((uint32_t*)*esp) = argc;

    // Adds a fake return address to the stack
    *esp -= 4;
    *((uint32_t*)*esp) = 0;

    palloc_free_page(argv);
}

/* 
 * Starts a new kernel thread running a user program loaded from CMDLINE. 
 * The new thread may be scheduled (and may even exit) before process_execute() 
 * returns.  Returns the new process's thread id, or TID_ERROR if the thread 
 * could not be created. 
 */
tid_t
process_execute(const char *cmdline)
{
#ifndef COMMAND_ARGUMENTS
    char space[] = " ";
    if (strcspn(cmdline,space) != strlen(cmdline)) {
        printf("Command line arguments not implemented, exiting...\n");
	return TID_ERROR;
    }
#endif

    char *cmdline_copy = NULL;
    tid_t tid = TID_ERROR;

    // Make a copy of CMDLINE to avoid a race condition between the caller and load() 
    cmdline_copy = palloc_get_page(0);
    if (cmdline_copy == NULL) 
        return TID_ERROR;
    
    strlcpy(cmdline_copy, cmdline, PGSIZE);

    struct process_block *init_block = palloc_get_page(0);
    init_block->pid = -1;
    init_block->cmdline = cmdline_copy;
    init_block->finished = false;
    init_block->exit_code = -1;
    semaphore_init(&init_block->init_child, 0);
    semaphore_init(&init_block->waiter, 0);

    // Create a Kernel Thread for the new process
    tid = thread_create(cmdline, PRI_DEFAULT, start_process, init_block);

    semaphore_down(&init_block->init_child);
    list_push_back(&(thread_current()->children_list), &(init_block->elem));
    // timer_msleep(10);

    return tid;
}

/* 
 * A thread function to load a user process and start it running. 
 * CMDLINE is assumed to contain an executable file name with no arguments.
 * If arguments are passed in CMDLINE, the thread will exit imediately.
 */
static void
start_process(void *cmdline)
{
    bool success = false;

    struct process_block *init_block = cmdline;

    // Initialize interrupt frame and load executable. 
    struct intr_frame pif;
    memset(&pif, 0, sizeof pif);
    pif.gs = pif.fs = pif.es = pif.ds = pif.ss = SEL_UDSEG;
    pif.cs = SEL_UCSEG;
    pif.eflags = FLAG_IF | FLAG_MBS;
    success = load((char*)init_block->cmdline, &pif.eip, &pif.esp);
    if (success) {
        push_command(init_block->cmdline, &pif.esp);
    }
    // palloc_free_page(&init_block->cmdline);

    if (!success) {
        thread_exit();
    }
    init_block->pid = thread_current()->tid;
    thread_current()->problock = init_block;
    semaphore_up(&init_block->init_child);

    // Start the user process by simulating a return from an
    // interrupt, implemented by intr_exit (in threads/intr-stubs.S).  
    // Because intr_exit takes all of its arguments on the stack in 
    // the form of a `struct intr_frame',  we just point the stack 
    // pointer (%esp) to our stack frame and jump to it.
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&pif) : "memory");
    NOT_REACHED();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in Lab 3.  
   For now, it does nothing. */
int
process_wait(tid_t child_tid UNUSED)
{
    struct process_block *child_block = NULL;
    struct list_elem *curr = NULL;
    struct list *child_list = &(thread_current()->children_list);
    if(!list_empty(child_list)){
        for(curr = list_front(child_list); curr != list_end(child_list); curr = list_next(curr)){
            struct process_block *problock = list_entry(curr, struct process_block, elem);
            if(problock->pid == child_tid){
                child_block = problock;
                break;
            }
        }
    }
    if(child_block == NULL)
        return -1;

    if(child_block->finished == false){
        semaphore_down(&(child_block->waiter));
        list_remove(curr);
    }

    return child_block->exit_code;
}

/* Free the current process's resources. */
void
process_exit(void)
{
    struct thread *cur = thread_current();
    uint32_t *pd;

    cur->problock->finished = true;
    semaphore_up(&cur->problock->waiter);
    
    /* Destroy the current process's page directory and switch back
       to the kernel-only page directory. */
    pd = cur->pagedir;
    if (pd != NULL) {
        /* Correct ordering here is crucial.  We must set
           cur->pagedir to NULL before switching page directories,
           so that a timer interrupt can't switch back to the
           process page directory.  We must activate the base page
           directory before destroying the process's page
           directory, or our active page directory will be one
           that's been freed (and cleared). */
        cur->pagedir = NULL;
        pagedir_activate(NULL);
        pagedir_destroy(pd);
    }
}

/****************** System Call Implementations ********************/

// *****************************************************************
// CMPS111 Lab 3 : Put your new system call implementatons in your 
// own source file. Define them in your header file and include 
// that .h in this .c file.
// *****************************************************************

void sys_exit(int status) 
{
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_current()->problock->exit_code = status;
  thread_exit();
}

void exit_handler(struct intr_frame *f) 
{
  int exitcode;
  umem_read(f->esp + 4, &exitcode, sizeof(exitcode));

  sys_exit(exitcode);
}

/*
 * BUFFER+0 and BUFFER+size should be valid user adresses
 */
static uint32_t sys_write(int fd, const void *buffer, unsigned size)
{
  umem_check((const uint8_t*) buffer);
  umem_check((const uint8_t*) buffer + size - 1);

  int ret = -1;

  lock_acquire(&sys_lock);
  if (fd == 1) { // write to stdout
    putbuf(buffer, size);
    ret = size;
  }

  struct file_info *fi = NULL;
  if(!list_empty(&thread_current()->file_list)){
      for(struct list_elem *curr = list_front(&thread_current()->file_list); curr != list_end(&thread_current()->file_list);
          curr = list_next(curr)){
          struct file_info *curr_info = list_entry(curr, struct file_info, elem);
          if(curr_info->id == fd){
              fi = curr_info;
              break;
          }
      }
      if(fi == NULL){
          lock_release(&sys_lock);
          return -1;
      } 
  }else{
      lock_release(&sys_lock);
      return -1;
  }

  ret = file_write(fi->filename, buffer, size);
  lock_release(&sys_lock);

  return (uint32_t) ret;
} 

void write_handler(struct intr_frame *f)
{
    int fd;
    const void *buffer;
    unsigned size;

    umem_read(f->esp + 4, &fd, sizeof(fd));
    umem_read(f->esp + 8, &buffer, sizeof(buffer));
    umem_read(f->esp + 12, &size, sizeof(size));
    
    f->eax = sys_write(fd, buffer, size);
}

static bool sys_create(char* filename, int size){
    lock_acquire(&sys_lock);
    bool status = filesys_create(filename, size, false);
    lock_release(&sys_lock);
    return status;
}

void create_handler(struct intr_frame *f)
{
    char* file;
    int size;

    umem_read(f->esp + 4, &file, sizeof(file));
    umem_read(f->esp + 8, &size, sizeof(size));

    bool status = sys_create(file, size);
    f->eax = status;
}

static int sys_open(char* filename){
    struct file_info *fi = palloc_get_page(0);
    lock_acquire(&sys_lock);
    struct file *open_file = filesys_open(filename);
    if(open_file == NULL){
        lock_release(&sys_lock);
        return -1;
    }
    fi->filename = open_file;
    fi->id = thread_current()->open_file_count + 3;
    thread_current()->open_file_count++;
    list_push_back(&thread_current()->file_list, &fi->elem);
    lock_release(&sys_lock);

    return fi->id;
}

void open_handler(struct intr_frame *f)
{
    char* file;

    umem_read(f->esp + 4, &file, sizeof(file));

    int status = sys_open(file);
    f->eax = status;
}

static int sys_read(int file_num, void* buffer, int size){
    lock_acquire(&sys_lock);
    struct file_info *fi = NULL;
    if(!list_empty(&thread_current()->file_list)){
        for(struct list_elem *curr = list_front(&thread_current()->file_list); curr != list_end(&thread_current()->file_list);
          curr = list_next(curr)){
              struct file_info *curr_info = list_entry(curr, struct file_info, elem);
              if(curr_info->id == file_num){
                  fi = curr_info;
                  break;
              }
          }
          if(fi == NULL){
            lock_release(&sys_lock);
            return -1;
          } 
    }else{
      lock_release(&sys_lock);
      return -1;
    }

    int status = file_read(fi->filename, buffer, size);
    lock_release(&sys_lock);
    return status;
}

void read_handler(struct intr_frame *f)
{
    int file_number;
    void* buffer;
    int size;

    umem_read(f->esp + 4, &file_number, sizeof(file_number));
    umem_read(f->esp + 8, &buffer, sizeof(buffer));
    umem_read(f->esp + 12, &size, sizeof(size));

    int status = sys_read(file_number, buffer, size);
    f->eax = status;
}

static int sys_filesize(int file_number){
    lock_acquire(&sys_lock);
    struct file_info *fi = NULL;
    if(!list_empty(&thread_current()->file_list)){
        for(struct list_elem *curr = list_front(&thread_current()->file_list); curr != list_end(&thread_current()->file_list);
          curr = list_next(curr)){
              struct file_info *curr_info = list_entry(curr, struct file_info, elem);
              if(curr_info->id == file_number){
                  fi = curr_info;
                  break;
              }
          }
          if(fi == NULL) {
            lock_release(&sys_lock);
            return -1;
          }
    }else{
      lock_release(&sys_lock);
      return -1;
    }

    int status = file_length(fi->filename);
    lock_release(&sys_lock);
    return status;
}

void filesize_handler(struct intr_frame *f)
{
    int file_number;

    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    int status = sys_filesize(file_number);
    f->eax = status;
}

static void sys_close(int file_number){
    lock_acquire(&sys_lock);
    struct file_info *fi = NULL;
    if(!list_empty(&thread_current()->file_list)){
        for(struct list_elem *curr = list_front(&thread_current()->file_list); curr != list_end(&thread_current()->file_list);
          curr = list_next(curr)){
              struct file_info *curr_info = list_entry(curr, struct file_info, elem);
              if(curr_info->id == file_number){
                  fi = curr_info;
                  break;
              }
          }
          if(fi == NULL){
              lock_release(&sys_lock);
              return;
          }
    }else{
      lock_release(&sys_lock);
      return;
    }

    thread_current()->open_file_count --;
    file_close(fi->filename);
    list_remove(&fi->elem);
    lock_release(&sys_lock);

}

void close_handler(struct intr_frame *f)
{
    int file_number;

    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    sys_close(file_number);
}

static int sys_wait(int file_number){
    int status = process_wait(file_number);
    return status;
}

void wait_handler(struct intr_frame *f)
{
    int file_number;
    
    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    int status = sys_wait(file_number);
    f->eax = status;
}

static int sys_exec(void* command_line){
    lock_acquire(&sys_lock);
    int status = process_execute(command_line);
    lock_release(&sys_lock);
    return status;
}

void exec_handler(struct intr_frame *f)
{
    void* command_line;
    
    umem_read(f->esp + 4, &command_line, sizeof(command_line));

    int status = sys_exec(command_line);
    f->eax = status;
}