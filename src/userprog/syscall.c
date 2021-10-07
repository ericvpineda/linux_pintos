#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    thread_current()->pcb->exit_code = args[1];
    process_exit();
  }

  else if (args[0] == SYS_WRITE && args[1] == 1) {
    putbuf((char*) args[2], args[3]);
    f->eax = args[3];
  }

  else if (args[0] == SYS_PRACTICE) {
    f->eax = args[1] + 1;
  }

  else if (args[0] == SYS_HALT) {
    shutdown_power_off();
  }

  else if (args[0] == SYS_EXEC) {
    f->eax = process_execute((char*) args[1]);
  }

  else if (args[0] == SYS_WAIT) {
    return;
    //f->eax = process_wait((char*) args[1]);
  }

}