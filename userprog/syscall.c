/* 
 * This file is derived from source code for the Pintos
 * instructional operating system which is itself derived
 * from the Nachos instructional operating system. The 
 * Nachos copyright notice is reproduced in full below. 
 *
 * Copyright (C) 1992-1996 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose, without fee, and
 * without written agreement is hereby granted, provided that the
 * above copyright notice and the following two paragraphs appear
 * in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
 * AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
 * HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
 * BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
 * MODIFICATIONS.
 *
 * Modifications Copyright (C) 2017 David C. Harrison. All rights reserved.
 */

#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>

#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/umem.h"
#include "threads/lock.h"

static void syscall_handler(struct intr_frame *);

static void write_handler(struct intr_frame *);
static void exit_handler(struct intr_frame *);

// Added
static void create_handler(struct intr_frame *);
static void open_handler(struct intr_frame *);
static void read_handler(struct intr_frame *);
static void filesize_handler(struct intr_frame *);
static void close_handler(struct intr_frame *);
static void wait_handler(struct intr_frame *);
static void exec_handler(struct intr_frame *);

struct lock sys_lock;
//End added

void
syscall_init (void)
{
  lock_init(&sys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f)
{
  int syscall;
  ASSERT( sizeof(syscall) == 4 ); // assuming x86

  // The system call number is in the 32-bit word at the caller's stack pointer.
  umem_read(f->esp, &syscall, sizeof(syscall));

  // Store the stack pointer esp, which is needed in the page fault handler.
  // Do NOT remove this line
  thread_current()->current_esp = f->esp;

  switch (syscall) {
  case SYS_HALT: 
    shutdown_power_off();
    break;

  case SYS_EXIT: 
    exit_handler(f);
    break;
      
  case SYS_WRITE: 
    write_handler(f);
    break;
  
  //Added
  case SYS_CREATE:
    create_handler(f);
    break;

  case SYS_OPEN:
    open_handler(f);
    break;

  case SYS_READ:
    read_handler(f);
    break;

  case SYS_FILESIZE:
    filesize_handler(f);
    break;

  case SYS_CLOSE:
    close_handler(f);
    break;

  case SYS_WAIT:
    wait_handler(f);
    break;

  case SYS_EXEC:
    exec_handler(f);
    break;
  //End Added

  default:
    printf("[ERROR] system call %d is unimplemented!\n", syscall);
    thread_exit();
    break;
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

static void exit_handler(struct intr_frame *f) 
{
  int exitcode;
  umem_read(f->esp + 4, &exitcode, sizeof(exitcode));

  sys_exit(exitcode);
}

// Added


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

static void write_handler(struct intr_frame *f)
{
    int fd;
    const void *buffer;
    unsigned size;

    umem_read(f->esp + 4, &fd, sizeof(fd));
    umem_read(f->esp + 8, &buffer, sizeof(buffer));
    umem_read(f->esp + 12, &size, sizeof(size));
    
    f->eax = sys_write(fd, buffer, size);
}

bool sys_create(char* filename, int size){
    lock_acquire(&sys_lock);
    bool status = filesys_create(filename, size, false);
    lock_release(&sys_lock);
    return status;
}

static void create_handler(struct intr_frame *f)
{
    char* file;
    int size;

    umem_read(f->esp + 4, &file, sizeof(file));
    umem_read(f->esp + 8, &size, sizeof(size));

    bool status = sys_create(file, size);
    f->eax = status;
}

int sys_open(char* filename){
    struct file_info *fi = palloc_get_page(0);
    lock_acquire(&sys_lock);
    struct file *open_file = filesys_open(filename);
    if(open_file == NULL){
// printf("returning null");
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
static void open_handler(struct intr_frame *f)
{
    char* file;

    umem_read(f->esp + 4, &file, sizeof(file));

    int status = sys_open(file);
    f->eax = status;
}

int sys_read(int file_num, void* buffer, int size){
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

static void read_handler(struct intr_frame *f)
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

int sys_filesize(int file_number){
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

static void filesize_handler(struct intr_frame *f)
{
    int file_number;

    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    int status = sys_filesize(file_number);
    f->eax = status;
}

void sys_close(int file_number){
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

    file_close(fi->filename);
    list_remove(&fi->elem);
    lock_release(&sys_lock);

}

static void close_handler(struct intr_frame *f)
{
    int file_number;

    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    sys_close(file_number);
}

int sys_wait(int file_number){
    int status = process_wait(file_number);
    return status;
}

static void wait_handler(struct intr_frame *f)
{
    int file_number;
    
    umem_read(f->esp + 4, &file_number, sizeof(file_number));

    int status = sys_wait(file_number);
    f->eax = status;
}

int sys_exec(void* command_line){
    lock_acquire(&sys_lock);
    int status = process_execute(command_line);
    lock_release(&sys_lock);
    return status;
}

static void exec_handler(struct intr_frame *f)
{
    void* command_line;
    
    umem_read(f->esp + 4, &command_line, sizeof(command_line));

    int status = sys_exec(command_line);
    f->eax = status;
}
//End Added