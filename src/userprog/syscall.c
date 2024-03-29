/* Includes pagedir.c */
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include <stdlib.h>

#include "threads/vaddr.h"
#include "pagedir.h"
#include <string.h>
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include <float.h>

#include "filesys/directory.h"

struct lock syscall_lock;

/* Prototype functions */
static void syscall_handler(struct intr_frame*);
void syscall_init(void) {
  lock_init(&syscall_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

struct file_dir* get_file_wrapper(uint32_t* fd);
bool check_valid_location(void* file_name);
void validate_buffer(void* ptr, size_t size);
void validate_pointer(void* ptr, size_t size);
void validate_string(char* string);

/* Main syscall handler */
static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  validate_pointer(args, sizeof(uint32_t));
  validate_pointer(&args[1], sizeof(args[1]));
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  /* Create -- syscall */
  if (args[0] == SYS_CREATE) {
    lock_acquire(&syscall_lock);
    char* file_name = (char*)args[1];
    size_t file_size = (size_t)args[2];

    /* Check each byte located in valid vaddr and pagedir */
    if (!check_valid_location((void*)file_name)) {
      f->eax = 0;
      thread_current()->pcb->wait_status->exit_code = -1;
      lock_release(&syscall_lock);
      return process_exit();
    }

    /* Check NULL, empty string file_name, file_name already exists */
    if (!filesys_create(file_name, file_size, 0)) {
      f->eax = 0;
    } else {
      f->eax = 1;
    }
    lock_release(&syscall_lock);
  }

  /* Filesize -- Syscall */
  if (args[0] == SYS_FILESIZE) {
    lock_acquire(&syscall_lock);
    struct file_dir* open_file_wrapper = get_file_wrapper(args);
    struct file* open_file_table = open_file_wrapper->file;
    if (open_file_table) {
      f->eax = file_length(open_file_table);
    }
    lock_release(&syscall_lock);
  }

  /* Close -- Syscall */
  if (args[0] == SYS_CLOSE) {
    lock_acquire(&syscall_lock);
    struct process* pcb = thread_current()->pcb;
    int fd = args[1];
    int fd_index = pcb->fd_index;
    struct file_dir* open_file_wrapper = get_file_wrapper(args);
    if (open_file_wrapper != NULL && fd >= 3 && fd < fd_index &&
        check_valid_location((void*)open_file_wrapper->name)) {
      if (open_file_wrapper->isdir) {
        struct dir* open_dir_table = open_file_wrapper->dir;
        dir_close(open_dir_table);
      } else {
        struct file* open_file_table = open_file_wrapper->file;
        file_close(open_file_table);
      }
      list_remove(&open_file_wrapper->elem);
    }
    lock_release(&syscall_lock);
  }

  /* Tell -- syscall */
  if (args[0] == SYS_TELL) {
    lock_acquire(&syscall_lock);
    struct file_dir* open_file_wrapper = get_file_wrapper(args);
    struct file* open_file_table = open_file_wrapper->file;
    if (open_file_table) {
      off_t curr_byte_pos = file_tell(open_file_table);
      f->eax = curr_byte_pos;
    }
    lock_release(&syscall_lock);
  }

  /* Seek -- syscall */
  if (args[0] == SYS_SEEK) {
    lock_acquire(&syscall_lock);
    struct file_dir* open_file_wrapper = get_file_wrapper(args);
    struct file* open_file_table = open_file_wrapper->file;
    if (open_file_table) {
      file_seek(open_file_table, (off_t)args[2]);
    }
    lock_release(&syscall_lock);
  }

  /* Remove -- syscall */
  if (args[0] == SYS_REMOVE) {
    lock_acquire(&syscall_lock);
    char* file_name = (char*)args[1];
    bool res = filesys_remove(file_name);
    f->eax = !res ? 0 : 1;
    lock_release(&syscall_lock);
  }

  /* Open -- syscall */
  if (args[0] == SYS_OPEN) {
    lock_acquire(&syscall_lock);
    char* file_name = (char*)args[1];
    struct process* pcb = thread_current()->pcb;
    int fd_index = pcb->fd_index;

    if (!check_valid_location((void*)file_name) || !file_name) {
      f->eax = 0;
      thread_current()->pcb->wait_status->exit_code = -1;
      lock_release(&syscall_lock);
      return process_exit();
    } else if (*file_name == '\0') {
      f->eax = -1;
      lock_release(&syscall_lock);
      return;
    }

    // potential bugs bc filesys_open_dir always returns a dir
    struct file* open_file = filesys_open(file_name);
    struct dir* open_dir = filesys_open_dir(file_name);

    /* If no open file for file_name exists, return error code */
    if (!open_file && !open_dir) {
      f->eax = -1;
      thread_current()->pcb->wait_status->exit_code = 0;
      lock_release(&syscall_lock);
      return;
    }

    // initialize fd wrapper for file
    struct file_dir* file_dir = malloc(sizeof(struct file_dir));
    file_dir->file = open_file;
    file_dir->dir = open_dir;
    // need to change this to accommodate for absolute and relative paths
    file_dir->name = file_name;
    file_dir->id = fd_index;
    if (open_file == NULL) {
      file_dir->isdir = true;
    } else {
      file_dir->isdir = false;
    }

    /* Else, add open_file to FDT */
    list_push_front(&pcb->fdt, &file_dir->elem);

    /* Return fd to user process */
    f->eax = fd_index;
    pcb->fd_index++;

    lock_release(&syscall_lock);
  }

  /* Exit -- syscall */
  if (args[0] == SYS_EXIT) {
    lock_acquire(&syscall_lock);
    f->eax = args[1];
    thread_current()->pcb->wait_status->exit_code = args[1];
    lock_release(&syscall_lock);
    process_exit();
  }

  /* Read -- syscall */
  if (args[0] == SYS_READ) {
    lock_acquire(&syscall_lock);
    struct process* pcb = thread_current()->pcb;
    int fd_index = pcb->fd_index;
    int fd = (int)args[1];
    char* buffer = (char*)args[2];
    size_t size = (size_t)args[3];

    /* Check buffer correct location & buffer valid fd */
    if (fd == 1 || fd < 0 || fd >= fd_index || !check_valid_location((void*)buffer)) {
      f->eax = -1;
      thread_current()->pcb->wait_status->exit_code = -1;
      lock_release(&syscall_lock);
      return process_exit();
    }

    // Read from stdin
    if (!fd) {
      input_init();
      uint8_t typing_key;
      size_t total = 0;
      while ((size - total) && (typing_key = input_getc())) {
        total += 1;
        buffer[total] = typing_key;
      }
      f->eax = total;
      lock_release(&syscall_lock);
      return;
    }

    /* Else read from open file table */
    struct file_dir* file_wrapper = get_file_wrapper(args);

    // disallow reads on directories
    if (file_wrapper->isdir) {
      f->eax = -1;
      lock_release(&syscall_lock);
      return;
    }
    struct file* file_name = file_wrapper->file;
    if (file_name) {
      size_t bytes_read = 0;
      size_t total = 0;
      while ((bytes_read = file_read(file_name, buffer, size - total))) {
        total += bytes_read;
      }
      f->eax = total;
    } else {
      /* File could not be read */
      f->eax = -1;
    }
    lock_release(&syscall_lock);
  }

  /* Write -- syscall */
  if (args[0] == SYS_WRITE) {
    lock_acquire(&syscall_lock);
    int fd = (int)args[1];
    char* buffer = (char*)args[2];
    off_t size = (off_t)args[3];
    struct process* pcb = thread_current()->pcb;
    int fd_index = pcb->fd_index;

    /* Check invalid fd and vaddr/page locations */
    if (fd <= 0 || fd >= fd_index || !check_valid_location((void*)buffer)) {
      f->eax = -1;
      thread_current()->pcb->wait_status->exit_code = -1;
      lock_release(&syscall_lock);
      return process_exit();
    }

    /* Check if fd is stdout */
    if (fd == 1) {
      putbuf((char*)args[2], args[3]);
      f->eax = args[3];
      lock_release(&syscall_lock);
      return;
    }

    /* Else read from fdt */
    struct file_dir* file_wrapper = get_file_wrapper(args);

    // disallow writes on directories
    if (file_wrapper->isdir) {
      f->eax = -1;
      lock_release(&syscall_lock);
      return;
    }
    struct file* file_name = file_wrapper->file;
    if (file_name) {
      off_t bytes_read = 0;
      off_t total = 0;
      while ((bytes_read = file_write(file_name, buffer, size - total))) {
        total += bytes_read;
      }
      f->eax = total;
    } else {
      /* File could not be read */
      f->eax = -1;
    }
    lock_release(&syscall_lock);
  }

  /* Practice -- syscall */
  if (args[0] == SYS_PRACTICE) {
    lock_acquire(&syscall_lock);
    f->eax = args[1] + 1;
    lock_release(&syscall_lock);
  }

  /* compute E -- syscall */
  if (args[0] == SYS_COMPUTE_E) {
    lock_acquire(&syscall_lock);
    int e_value = (int)args[1];
    int res = sys_sum_to_e(e_value);
    f->eax = res;
    lock_release(&syscall_lock);
  }

  /* Halt -- syscall */
  else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  /* Exec -- syscall */
  else if (args[0] == SYS_EXEC) {
    validate_string((char*)args[1]);
    f->eax = process_execute((char*)args[1]);
  }

  /* Wait -- syscall */
  else if (args[0] == SYS_WAIT) {
    f->eax = process_wait(args[1]);
  }

  /* Change Directory (chdir) syscall */
  else if (args[0] == SYS_CHDIR) {
    lock_acquire(&syscall_lock);
    f->eax = filesys_chdir((char*)args[1]);
    lock_release(&syscall_lock);
  }

  /* Make Directory (mkdir) syscall */
  else if (args[0] == SYS_MKDIR) {
    lock_acquire(&syscall_lock);
    f->eax = filesys_create((char*)args[1], 0, 1);
    lock_release(&syscall_lock);
  }

  /* Read Directory (readdir) syscall */
  // Potential BUG: Not sure if . or .. will be returned by dir_readdir
  else if (args[0] == SYS_READDIR) {
    lock_acquire(&syscall_lock);
    struct file_dir* potential_directory = get_file_wrapper(args);
    if (potential_directory->isdir == false) {
      f->eax = false;
      lock_release(&syscall_lock);
      return;
    }

    f->eax = dir_readdir(potential_directory->dir, (char*)args[2]);
    lock_release(&syscall_lock);
  }

  /* Is Directory (isdir) syscall */
  else if (args[0] == SYS_ISDIR) {
    lock_acquire(&syscall_lock);
    struct file_dir* potential_directory = get_file_wrapper(args);
    f->eax = potential_directory->isdir;
    lock_release(&syscall_lock);
  }

  /* inumber syscall */
  else if (args[0] == SYS_INUMBER) {
    lock_acquire(&syscall_lock);
    struct file_dir* file_dir = get_file_wrapper(args);
    if (file_dir->isdir) {
      f->eax = inode_get_inumber(file_dir->dir->inode);
    } else {
      f->eax = inode_get_inumber(file_dir->file->inode);
    }

    lock_release(&syscall_lock);
  }
}

// HELPER METHODS

/* Get file associated with fd */
struct file_dir* get_file_wrapper(uint32_t* args) {
  int fd = (int)args[1];
  struct process* pcb = thread_current()->pcb;
  int unused_fd = (int)pcb->fd_index;
  if (fd < unused_fd || fd >= 0) {
    struct list_elem* e;
    for (e = list_begin(&pcb->fdt); e != list_end(&pcb->fdt); e = list_next(e)) {
      struct file_dir* tmp = list_entry(e, struct file_dir, elem);
      if (tmp->id == fd) {
        return tmp;
      }
    }
  }
  return NULL;
}

/* Check file_name valid location in vaddr && pagedir */
bool check_valid_location(void* file_name) {
  struct process* pcb = thread_current()->pcb;
  if (!is_user_vaddr(file_name) || !pagedir_get_page(pcb->pagedir, file_name)) {
    return 0;
  }
  return 1;
}

/* Validates ptr by exiting with code -1 if ptr is an invalid memory addr or invalid pointer */
void validate_pointer(void* ptr, size_t size) {
  if (!check_valid_location(ptr) || !check_valid_location(ptr + size)) {
    thread_current()->pcb->wait_status->exit_code = -1;
    return process_exit();
  }
}

/* Validates string by exiting with code -1 if string maps to invalid pg or its contents are not in user space */
void validate_string(char* string) {
  if (is_user_vaddr(string)) {
    char* pg = pagedir_get_page(thread_current()->pcb->pagedir, string);
    if (pg == NULL || !check_valid_location(string + strlen(pg) + 1)) {
      thread_current()->pcb->wait_status->exit_code = -1;
      return process_exit();
    }
  }
}