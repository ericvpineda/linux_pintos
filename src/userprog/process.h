#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>
#include "filesys/file.h"

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  struct file *fdt[128];      /* ADDED: File descriptor table */
  int fd_index;               /* ADDED: Next unused fd index */
  struct file *running_file;  /* ADDED: File process currently running */
  struct list children; /* ADDED: List of all children for this thread (elems will be a wait_status struct). */
  struct wait_status *wait_status; /* ADDED: This thread's wait status. */
};

/* Shared data struct between parent and child so that parent can wait on child thread. */
struct wait_status {
   tid_t tid;
   struct semaphore sema;
   int exit_code;
   int refs_count;
   struct lock refs_lock;
   bool already_waited;
   struct list_elem elem;
};

/* The load_data struct is used to track the load success state of child threads */
struct load_data {
  char* file_name;
  struct wait_status *wait_status;
  struct semaphore load_sema;
  bool loaded;
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

#endif /* userprog/process.h */
