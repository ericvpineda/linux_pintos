#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"

static thread_func start_process NO_RETURN;
static bool load(const char* file_name, void (**eip)(void), void** esp);

/* Initializes user programs in the system by ensuring the main
   thread has a minimal PCB so that it can execute and wait for
   the first user process. Any additions to the PCB should be also
   initialized here if main needs those members */
void userprog_init(void) {
  struct thread* t = thread_current();
  bool success;

  /* Allocate process control block
     It is imoprtant that this is a call to calloc and not malloc,
     so that t->pcb->pagedir is guaranteed to be NULL (the kernel's
     page directory) when t->pcb is assigned, because a timer interrupt
     can come at any time and activate our pagedir */
  t->pcb = calloc(sizeof(struct process), 1);
  success = t->pcb != NULL;

  /* Initialize list of children */
  list_init(&t->pcb->children);

  // set flag to indicate if this is the first user process
  t->pcb->first_process = true;

  /* Kill the kernel if we did not succeed */
  ASSERT(success);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* file_name) {
  char* fn_copy;
  tid_t tid;
  struct thread* t = thread_current();

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page(0);
  strlcpy(fn_copy, file_name, PGSIZE);

  if (fn_copy == NULL)
    return TID_ERROR;

  /* Create load data struct to pass into start_process */
  struct load_data load_data;
  load_data.file_name = fn_copy;
  sema_init(&load_data.load_sema, 0);

  if (t->pcb->first_process) {
    // if this is the first user process, send down root dir
    load_data.cwd = dir_open_root();
    t->pcb->first_process = false;
  } else {
    // else, send down cwd
    load_data.cwd = dir_reopen(t->pcb->cwd);
  }

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create(fn_copy, PRI_DEFAULT, start_process, (void*)&load_data);

  if (tid == TID_ERROR) {
    palloc_free_page(fn_copy);
  } else {
    // if thread successfully created, down loading semaphore
    sema_down(&load_data.load_sema);
    if (load_data.loaded) {
      // if load was successful, add wait status to the parent's children
      list_push_back(&t->pcb->children, &load_data.wait_status->elem);
    } else {
      palloc_free_page(fn_copy);
      tid = TID_ERROR;
    }
  }
  return tid;
}

/* A thread function that loads a user process and starts it running. */
static void start_process(void* file_name_) {
  struct load_data* load_data = (struct load_data*)file_name_;
  char* file_name = (char*)load_data->file_name;
  struct thread* t = thread_current();
  struct intr_frame if_;
  bool success, pcb_success;

  /* Allocate process control block */
  struct process* new_pcb = malloc(sizeof(struct process));

  /* Initialize children list for current thread */
  list_init(&new_pcb->children);

  success = pcb_success = new_pcb != NULL;

  /* Initialize first untaken fd index 
  fd_index 0, 1, 2 = stdin, stdout, stderr (if applicable) */
  new_pcb->fd_index = 3;

  /* Initialize FDT for this new PCB */
  list_init(&new_pcb->fdt);

  /* Parse string arguments seperated by spaces */
  char file_copy[strlen(file_name) + 1];
  strlcpy(file_copy, file_name, strlen(file_name) + 1);

  /* Iterate through filename copy to get argc */
  char *token_copy, *save_ptr_copy;
  int argc = 0;
  for (token_copy = strtok_r(file_copy, " ", &save_ptr_copy); token_copy != NULL;
       token_copy = strtok_r(NULL, " ", &save_ptr_copy)) {
    argc++;
  }

  char *token, *save_ptr;
  char* token_list[argc];
  int i = argc - 1;
  int tokens_start;

  /* Iterate through filename to extract tokens into token_list */
  for (token = strtok_r(file_name, " ", &save_ptr); token != NULL;
       token = strtok_r(NULL, " ", &save_ptr)) {
    token_list[i] = (char*)malloc(strlen(token) + 1);
    strlcpy(token_list[i], token, strlen(token) + 1);
    tokens_start = i;
    i--;
  }
  char* process_name = token_list[argc - 1];
  palloc_free_page(file_name);

  /* Initialize process control block */
  if (success) {
    // Ensure that timer_interrupt() -> schedule() -> process_activate()
    // does not try to activate our uninitialized pagedir
    new_pcb->pagedir = NULL;
    t->pcb = new_pcb;

    // Continue initializing the PCB as normal
    t->pcb->main_thread = t;
    strlcpy(t->pcb->process_name, process_name, sizeof t->name);

    // Set cwd of this user process
    t->pcb->cwd = load_data->cwd;
  }

  /* Initialize interrupt frame and load executable. */
  if (success) {
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;

    success = load(process_name, &if_.eip, &if_.esp);

    /* Handle failure with succesful PCB malloc. Must free the PCB */
    if (!success && pcb_success) {
      // Avoid race where PCB is freed before t->pcb is set to NULL
      // If this happens, then an unfortuantely timed timer interrupt
      // can try to activate the pagedir, but it is now freed memory
      struct process* pcb_to_free = t->pcb;
      t->pcb = NULL;
      free(pcb_to_free);
    }

    if (!success) {
      // free token_list args before exiting thread
      for (int i = argc - 1; i >= tokens_start; i--) {
        free(token_list[i]);
      }
      load_data->loaded = false;
      sema_up(&load_data->load_sema);
      thread_exit();
    }

    /* Added FPU code */
    int FPU_SIZE = 108;
    uint8_t fpu1[FPU_SIZE];
    uint8_t init_fpu1[FPU_SIZE];
    asm("fsave (%0); fninit; fsave (%1); frstor (%0)" : : "g"(&fpu1), "g"(&init_fpu1));
    for (int i = 0; i < FPU_SIZE; i++) {
      if_.st[i] = init_fpu1[i];
    }
    /* End of added FPU code */

    void* temp = if_.esp;

    /* Store string arguments onto the stack */
    for (int i = 0; i < argc; i++) {
      if_.esp -= strlen(token_list[i]) + 1;
      memcpy(if_.esp, token_list[i], strlen(token_list[i]) + 1);
    }

    /* Stack align the ESP */
    uintptr_t align = (uintptr_t)if_.esp - (4 * argc) - 16;
    while (align % 16 != 12) {
      if_.esp--;
      memset(if_.esp, 0, 1);
      align--;
    }

    /* Add a null pointer sentinel */
    if_.esp -= 4;
    memset(if_.esp, 0, 4);

    /* Store stack addresses of the string arguments onto the stack in reverse order */
    for (int i = 0; i < argc; i++) {
      if_.esp -= sizeof(void*);
      temp -= strlen(token_list[i]) + 1;
      memcpy(if_.esp, &temp, sizeof(void*));
    }
    // free token_list args now that we are done using it
    for (int i = argc - 1; i >= tokens_start; i--) {
      free(token_list[i]);
    }

    /* Store argv onto the stack */
    void* argv0_addr = if_.esp;
    if_.esp -= sizeof(void*);
    memcpy(if_.esp, &argv0_addr, sizeof(void*));

    /* Store argc onto the stack */
    if_.esp -= sizeof(int);
    memcpy(if_.esp, &argc, sizeof(int));

    /* Store a fake return address onto the stack */
    int fake_return = 0;
    if_.esp -= sizeof(void (*)(void));
    memcpy(if_.esp, &fake_return, sizeof(void (*)(void)));

    /* Initialize shared data struct for this process and its parent
       contingent upon successful load. */
    load_data->loaded = true;
    t->pcb->wait_status = malloc(sizeof(struct wait_status));

    // put wait_status struct into load_data so parent has access to it
    load_data->wait_status = t->pcb->wait_status;

    t->pcb->wait_status->refs_count = 2;
    t->pcb->wait_status->exit_code = -1;
    t->pcb->wait_status->tid = t->tid;
    t->pcb->wait_status->already_waited = false;
    sema_init(&t->pcb->wait_status->sema, 0);
    lock_init(&t->pcb->wait_status->refs_lock);
    sema_up(&load_data->load_sema);
  }

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
  NOT_REACHED();
}

/* Waits for process with PID child_pid to die and returns its exit status.
   If it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If child_pid is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given PID,w returns -1
   immediately, without waiting.
   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int process_wait(pid_t child_pid) {
  struct thread* t = thread_current();
  struct list* thread_children = &t->pcb->children;

  /* Search this thread's children for CHILD_PID and set child accordingly
     if found, or otherwise NULL. */
  struct wait_status* child = NULL;
  struct wait_status* curr_child;
  struct list_elem* e;
  for (e = list_begin(thread_children); e != list_end(thread_children); e = list_next(e)) {
    curr_child = list_entry(e, struct wait_status, elem);
    if (curr_child->tid == child_pid) {
      child = curr_child;
      break;
    }
  }

  /* Return -1 if child does not exist or process_wait() has already been called on it */
  if (child == NULL || child->already_waited) {
    return -1;
  }

  /* Prevent waiting on a process multiple times. */
  child->already_waited = true;

  /* Down the child thread's semaphore to wait for it to finish. */
  sema_down(&child->sema);

  /* Destroy the shared data (no need to decrement/check ref count because
     the child process is guaranteed to have finished at this point). */
  int exit_code = child->exit_code;
  list_remove(&child->elem);
  free(child);

  return exit_code;
}

/* Free the current process's resources. */
void process_exit(void) {
  struct thread* cur = thread_current();
  struct process* pcb = cur->pcb;
  uint32_t* pd;

  /* If this thread does not have a PCB, don't worry */
  if (pcb == NULL) {
    thread_exit();
    NOT_REACHED();
  }

  /* Close all files in the file descriptor table */
  while (!list_empty(&pcb->fdt)) {
    struct file_dir* file_dir = list_entry(list_pop_front(&pcb->fdt), struct file_dir, elem);
    struct file* tmp = file_dir->file;
    file_close(tmp);
    free(file_dir);
  }

  /* Close current running executable */
  file_close(pcb->running_file);
  pcb->running_file = NULL;

  /* Close CWD */
  dir_close(pcb->cwd);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pcb->pagedir;
  if (pd != NULL) {
    /* Correct ordering here is crucial.  We must set
         cur->pcb->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
    cur->pcb->pagedir = NULL;
    pagedir_activate(NULL);
    pagedir_destroy(pd);
  }

  printf("%s: exit(%d)\n", pcb->process_name, pcb->wait_status->exit_code);

  /* Up the wait_status's semaphore before exiting this process. */
  sema_up(&pcb->wait_status->sema);

  /* Decrement the ref_count of this process's wait_struct, freeing/removing
     it from its children list if its ref count hits 0. */
  lock_acquire(&pcb->wait_status->refs_lock);
  pcb->wait_status->refs_count--;
  lock_release(&pcb->wait_status->refs_lock);
  if (pcb->wait_status->refs_count == 0) {
    list_remove(&pcb->wait_status->elem);
    free(pcb->wait_status);
  }

  /* Decrement ref_counts of all children processes, freeing/removing
     them from the children list if their ref count hits 0. */
  struct wait_status* child = NULL;
  struct list_elem* e;
  for (e = list_begin(&pcb->children); e != list_end(&pcb->children); e = list_next(e)) {
    child = list_entry(e, struct wait_status, elem);
    lock_acquire(&child->refs_lock);
    child->refs_count--;
    lock_release(&child->refs_lock);
    if (child->refs_count == 0) {
      list_remove(&child->elem);
      free(child);
    }
  }

  /* Free the PCB of this process and kill this thread
    Avoid race where PCB is freed before t->pcb is set to NULL
    If this happens, then an unfortuantely timed timer interrupt
    can try to activate the pagedir, but it is now freed memory */
  struct process* pcb_to_free = cur->pcb;
  cur->pcb = NULL;
  free(pcb_to_free);

  thread_exit();
}

/* Sets up the CPU for running user code in the current
   thread. This function is called on every context switch. */
void process_activate(void) {
  struct thread* t = thread_current();

  /* Activate thread's page tables. */
  if (t->pcb != NULL && t->pcb->pagedir != NULL)
    pagedir_activate(t->pcb->pagedir);
  else
    pagedir_activate(NULL);

  /* Set thread's kernel stack for use in processing interrupts.
     This does nothing if this is not a user process. */
  tss_update();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32 /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr {
  unsigned char e_ident[16];
  Elf32_Half e_type;
  Elf32_Half e_machine;
  Elf32_Word e_version;
  Elf32_Addr e_entry;
  Elf32_Off e_phoff;
  Elf32_Off e_shoff;
  Elf32_Word e_flags;
  Elf32_Half e_ehsize;
  Elf32_Half e_phentsize;
  Elf32_Half e_phnum;
  Elf32_Half e_shentsize;
  Elf32_Half e_shnum;
  Elf32_Half e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr {
  Elf32_Word p_type;
  Elf32_Off p_offset;
  Elf32_Addr p_vaddr;
  Elf32_Addr p_paddr;
  Elf32_Word p_filesz;
  Elf32_Word p_memsz;
  Elf32_Word p_flags;
  Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /* Ignore. */
#define PT_LOAD 1           /* Loadable segment. */
#define PT_DYNAMIC 2        /* Dynamic linking info. */
#define PT_INTERP 3         /* Name of dynamic loader. */
#define PT_NOTE 4           /* Auxiliary info. */
#define PT_SHLIB 5          /* Reserved. */
#define PT_PHDR 6           /* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

static bool setup_stack(void** esp);
static bool validate_segment(const struct Elf32_Phdr*, struct file*);
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char* file_name, void (**eip)(void), void** esp) {
  struct thread* t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file* file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pcb->pagedir = pagedir_create();
  if (t->pcb->pagedir == NULL)
    goto done;
  process_activate();

  /* Open executable file. */
  file = filesys_open(file_name);
  if (file == NULL) {
    printf("load: %s: open failed\n", file_name);
    goto done;
  }

  /* Assign process running file */
  t->pcb->running_file = file;

  /* Prevent running file from being written to */
  file_deny_write(file);

  /* Read and verify executable header. */
  if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr ||
      memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 ||
      ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024) {
    printf("load: %s: error loading executable\n", file_name);
    goto done;
  }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) {
    struct Elf32_Phdr phdr;

    if (file_ofs < 0 || file_ofs > file_length(file))
      goto done;
    file_seek(file, file_ofs);

    if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
    file_ofs += sizeof phdr;
    switch (phdr.p_type) {
      case PT_NULL:
      case PT_NOTE:
      case PT_PHDR:
      case PT_STACK:
      default:
        /* Ignore this segment. */
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
      case PT_SHLIB:
        goto done;
      case PT_LOAD:
        if (validate_segment(&phdr, file)) {
          bool writable = (phdr.p_flags & PF_W) != 0;
          uint32_t file_page = phdr.p_offset & ~PGMASK;
          uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
          uint32_t page_offset = phdr.p_vaddr & PGMASK;
          uint32_t read_bytes, zero_bytes;
          if (phdr.p_filesz > 0) {
            /* Normal segment.
                     Read initial part from disk and zero the rest. */
            read_bytes = page_offset + phdr.p_filesz;
            zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
          } else {
            /* Entirely zero.
                     Don't read anything from disk. */
            read_bytes = 0;
            zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
          }
          if (!load_segment(file, file_page, (void*)mem_page, read_bytes, zero_bytes, writable))
            goto done;
        } else
          goto done;
        break;
    }
  }

  /* Set up stack. */
  if (!setup_stack(esp))
    goto done;

  /* Start address. */
  *eip = (void (*)(void))ehdr.e_entry;
  success = true;

done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

static bool install_page(void* upage, void* kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool validate_segment(const struct Elf32_Phdr* phdr, struct file* file) {
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off)file_length(file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr((void*)phdr->p_vaddr))
    return false;
  if (!is_user_vaddr((void*)(phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:
        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.
        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.
   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool load_segment(struct file* file, off_t ofs, uint8_t* upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
  ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT(pg_ofs(upage) == 0);
  ASSERT(ofs % PGSIZE == 0);

  file_seek(file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) {
    /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    /* Get a page of memory. */
    uint8_t* kpage = palloc_get_page(PAL_USER);
    if (kpage == NULL)
      return false;

    /* Load this page. */
    if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes) {
      palloc_free_page(kpage);
      return false;
    }
    memset(kpage + page_read_bytes, 0, page_zero_bytes);

    /* Add the page to the process's address space. */
    if (!install_page(upage, kpage, writable)) {
      palloc_free_page(kpage);
      return false;
    }

    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
  }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool setup_stack(void** esp) {
  uint8_t* kpage;
  bool success = false;

  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL) {
    success = install_page(((uint8_t*)PHYS_BASE) - PGSIZE, kpage, true);
    if (success)
      *esp = PHYS_BASE;
    else
      palloc_free_page(kpage);
  }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool install_page(void* upage, void* kpage, bool writable) {
  struct thread* t = thread_current();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page(t->pcb->pagedir, upage) == NULL &&
          pagedir_set_page(t->pcb->pagedir, upage, kpage, writable));
}

/* Returns true if t is the main thread of the process p */
bool is_main_thread(struct thread* t, struct process* p) { return p->main_thread == t; }

/* Gets the PID of a process */
pid_t get_pid(struct process* p) { return (pid_t)p->main_thread->tid; }