/* Includes pagedir.c */
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"

#include "threads/vaddr.h"
#include "pagedir.h"
#include <string.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"


static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

int check_file_exists(char *file_name, struct process *pcb, int fd_index);

struct file* get_file(uint32_t* fd);

bool check_valid_location (char *file_name, struct process *pcb);


static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  /* create -- syscall */
  if (args[0] == SYS_CREATE) {

    char *file_name = (char *)args[1];
    size_t file_size = (size_t)args[2];
    struct process* pcb = thread_current()->pcb;

    /* Check each byte located in valid vaddr and pagedir */
    if (!check_valid_location(file_name, pcb)) {
      f->eax = 0;
      thread_current()->pcb->exit_code = -1;
      return process_exit();
    }

    /* Check NULL, empty string file_name, file_name already exists */    
    if (!filesys_create(file_name, file_size)) {
      f->eax = 0;
      return;
    }
    f->eax = 1;
  }

  /* filesize -- Syscall */ 
  if (args[0] == SYS_FILESIZE) {
    struct file *open_file_table = get_file(args);
    if (open_file_table) {
      f->eax = inode_length(open_file_table->inode);
    }
  }

  /* close -- Syscall */
  if (args[0] == SYS_CLOSE) {
    struct file *open_file_table = get_file(args);
    if (open_file_table) {
      file_close(open_file_table);
    }
  }

  /* tell -- syscall */
  if (args[0] == SYS_TELL) {
    struct file *open_file_table = get_file(args);
    if (open_file_table) {
      off_t curr_byte_pos = file_tell(open_file_table);
      f->eax = curr_byte_pos;
    }
  }

  /* seek -- syscall */
  if (args[0] == SYS_SEEK) {
    struct file *open_file_table = get_file(args);
    if (open_file_table) {
      file_seek(open_file_table, args[1]);
    }
  }

  /* remove -- syscall */
  if (args[0] == SYS_REMOVE) {
    char *file_name = (char *) args[1];
    bool res = filesys_remove(file_name);
    f->eax = !res ? 0 : 1;
  }

  /* open -- syscall */
  if (args[0] == SYS_OPEN) {
    char *file_name = (char *) args[1];
    struct process* pcb = thread_current()->pcb;
    int fd_index = pcb->fd_index;
    if (!check_valid_location(file_name, pcb) || !file_name) {
      f->eax = 0;
      thread_current()->pcb->exit_code = -1;
      return process_exit();
    } else if (*file_name == '\0') {
      f->eax = -1;
      return;
    } 

      /* Create new open_file_table in process fdt */
    struct file* open_file_table = filesys_open(file_name);
    if (!open_file_table) {
      f->eax = -1;
      thread_current()->pcb->exit_code = 0;
      return;
    }
    
    /* Else create file */
    pcb->fdt[fd_index] = open_file_table;
    pcb->fdt[fd_index]->name = file_name;
    f->eax = fd_index;
    pcb->fd_index++;
  }

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    thread_current()->pcb->exit_code = args[1];
    process_exit();
  }

  if (args[0] == SYS_WRITE && args[1] == 1) {
    putbuf((char*) args[2], args[3]);
    f->eax = args[3];
  }

  if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }
}

int check_file_exists(char *file_name, struct process *pcb, int fd_index) {
  for (int i=3; i < fd_index; i++) {
    char *pcb_file = pcb->fdt[i]->name;
    if (!strcmp(file_name, pcb_file)) {
      return 1; 
    }
  }
  return 0;
}

struct file* get_file(uint32_t* args) {
  int fd = (int) args[0];
  struct process* pcb = thread_current()->pcb;
  if (fd < pcb->fd_index) {
    return pcb->fdt[fd];
  }
  return NULL;
}

bool check_valid_location (char *file_name, struct process *pcb) {
  if (!is_user_vaddr((void *)file_name) || 
      !pagedir_get_page(pcb->pagedir, (void *) file_name)) {
    return 0; 
  }
  return 1;
}
