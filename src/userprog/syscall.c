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


static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

int check_file_exists(char *file_name, struct process *pcb, int fd_index);

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  /* Create syscall */
  if (args[0] == SYS_CREATE) {

    char *file_name = (char *)args[1];
    size_t file_size = (size_t)args[2];
    struct process* pcb = thread_current()->pcb;
    int fd_index = pcb->fd_index;

    /* Check each byte located in valid vaddr and pagedir */
    if (!is_user_vaddr((void *)file_name) || 
      !pagedir_get_page(pcb->pagedir, (void *) file_name)) {
      f->eax = 0;
      thread_current()->pcb->exit_code = -1;
      process_exit();
    }

    /* Check NULL or empty string file_name */
    if (!file_name || !strcmp(file_name, "")) {
      f->eax = 0;
      thread_current()->pcb->exit_code = -1;
      process_exit();
    }
    
    /* Check valid file_name len and if file exist in fdt */
    if (strlen(file_name) > 255 || check_file_exists(file_name, pcb, fd_index)) {
      f->eax = 0;
      return;
    } 
    
    /* Else create new open_file_table in process fdt */
    if (filesys_create(file_name, file_size)) {
      struct file* open_file_table = filesys_open(file_name);
      pcb->fdt[fd_index] = open_file_table;
      pcb->fdt[fd_index]->name = file_name;
      pcb->fd_index++;
      f->eax = 1;
    } else {
      f->eax = 0;
    }
  }


  /* Open syscall */
  if (args[0] == SYS_OPEN) {
    // printf("Opening file..\n");
    // char *input_file = (char *) args[1];
    // printf("File name = %s\n", input_file);
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