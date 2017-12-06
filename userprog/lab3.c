
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
    char* argument = strtok_r(cmdline, " ", &pointer);
    for(; argument != NULL; argument = strtok_r(NULL, " ", &pointer)){
// printf("%s\n", argument);
        argv[argc] = argument;
// printf('%s\n', *((void**)*esp));
        argc++;
    }

    void* arg_addr[argc];

    // Add the commandline to the stack starting from rightmost argument
    for(int i = argc - 1; i >= 0; i--){
// printf("%s\n", argv[i]);
        int arg_len = strlen(argv[i])+1;
        *esp -= arg_len;
        memcpy(*esp, argv[i], arg_len);
// printf("%s\n", *((char**)*esp));
        arg_addr[i] = *esp;
    }

    // Word align with the stack pointer. DO NOT REMOVE THIS LINE.
    *esp = (void*) ((unsigned int) (*esp) & 0xfffffffc);

    // Add the terminating NULL to the stack
    *esp -= 4;
    *((uint32_t*) *esp) = 0;
// printf("%d\n", *((uint32_t*)*esp));

    // Add the addresses of the arguments on the stack to the stack
    for(int j = argc - 1; j > -1; j--){
        *esp -= 4;
        *((void**)*esp) = arg_addr[j];
// printf("%d\n", *((void**)*esp));
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

    // Some of you CMPS111 Lab 3 code will go here.
    //
    // One approach is to immediately call a function you've created in a
    // new file to do well on the "is it well written" part of the assesment.
    //
    // An equally good alternative is to simply move this function to your own
    // file, make it non-static, put it's declaration in a new header file and
    // include that .h file in this .c file. 
    //
    // Something else to note: You'll be doing address arithmetic here and 
    // that's one of only a handful of situations in which it is acceptable
    // to have comments inside functions. 
    //
    // As you advance the stack pointer by adding fixed and variable offsets
    // to it, add a SINGLE LINE comment to each logical block, a comment that 
    // describes what you're doing, and why.
    //
    // If nothing else, it'll remind you what you did when it doesn't work :)
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

    // Added
    struct process_block *init_block = palloc_get_page(0);
    init_block->pid = -1;
    init_block->cmdline = cmdline_copy;
    init_block->finished = false;
    semaphore_init(&init_block->init_child, 0);
    semaphore_init(&init_block->waiter, 0);
    //End Added

    // Create a Kernel Thread for the new process
    // tid = thread_create(cmdline, PRI_DEFAULT, start_process, cmdline_copy);
    tid = thread_create(cmdline, PRI_DEFAULT, start_process, init_block);

    //Added
    semaphore_down(&init_block->init_child);
    list_push_back(&(thread_current()->children_list), &(init_block->elem));
    //End Added
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

    //Added
    struct process_block *init_block = cmdline;
// printf("cmdline: %s\n", init_block->cmdline);
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
}

/* Free the current process's resources. */
void
process_exit(void)
{
    struct thread *cur = thread_current();
    uint32_t *pd;

    //Added
    cur->problock->finished = true;
    semaphore_up(&cur->problock->waiter);
    //End Added
    
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