CS 162 Group Repository
=======================

This repository contains code for CS 162 group projects.

Checklist:
- arg passing (completed)
- fpu init (in progress)
- syscalls (in progress)

Questions
- need to allocate more space for PCB (fd table)
- how to add stdin/out/err to pcb->fdt (process.c)
- do I need to catch file NULL for fd (syscall.c)
- need validate # args (syscall.c)
- does exit(-1) calls need f->eax return values? 
 
 Solved questions 
 - how to get page directory pd for pagedir_get_page -> pd in pcb (process.h)
 - fix circular dependency (file.c, inode.c, syscall.c) -> include filesys/filesys.h

Issues:

Log
- process.c -- added fdt struct, fd_pos (current untaken fd)
- created filesys/misc_utils.c -> TEMP FIX: cyclic dependencies in syscall.c 
- created create -- syscall (syscall.c)
- modify struct file to have name field (file.h)
- passing tests: all create -- syscall test
- clean up create -- syscall logic 
- implemented filesize -- syscall (need TESTING)
- implemented close -- syscall 
- passing all tests: close -- syscall 
- implemented tell -- syscall (need TESTING)
- implemeneted remove -- syscall (need TESTING)
    - need pass test -- /base/syn-remove (need WRITE, READ syscalls)
    - need to modify fd connected to file?
- implemented open -- syscall 
- passing tests: all open -- syscalls (syscall.c)
- implemented read -- syscall 
- passed all tests: read -- syscall 
    - need test stdin calls (personal TEST) 

 

Note:
- c. files has .h files as dependency 
- build in files in /lib folder
- check valid user virt addr (vaddr.h), valid page (pagedir.h)
- num fd connected to inode in inode open_cnt (inode.c)

