#ifndef THREADS_SWITCH_H
#define THREADS_SWITCH_H

#ifndef __ASSEMBLER__
/* switch_thread()'s stack frame. */
struct switch_threads_frame {
  int8_t st[108];
  // uint32_t st1;
  // uint32_t st2;
  // uint32_t st3;
  // uint32_t st4;
  // uint32_t st5;
  // uint32_t st6;
  // uint32_t st7;
  //uint32_t esp;
  uint32_t edi;        /* 32: Saved %edi. */
  uint32_t esi;        /* 36: Saved %esi. */
  uint32_t ebp;        /* 40: Saved %ebp. */
  uint32_t ebx;        /* 44: Saved %ebx. */
  void (*eip)(void);   /* 48: Return address. */
  struct thread* cur;  /* 52: switch_threads()'s CUR argument. */
  struct thread* next; /* 56: switch_threads()'s NEXT argument. */
};

/* Switches from CUR, which must be the running thread, to NEXT,
   which must also be running switch_threads(), returning CUR in
   NEXT's context. */
struct thread* switch_threads(struct thread* cur, struct thread* next);

/* Stack frame for switch_entry(). */
struct switch_entry_frame {
  void (*eip)(void);
};

void switch_entry(void);

/* Pops the CUR and NEXT arguments off the stack, for use in
   initializing threads. */
void switch_thunk(void);
#endif

/* Offsets used by switch.S. */
#define SWITCH_CUR 128
#define SWITCH_NEXT 132

#endif /* threads/switch.h */
