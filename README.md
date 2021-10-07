CS 162 Group Repository
=======================

This repository contains code for CS 162 group projects.

Log
- passed all task 3 tests :)

Issues:
- none :)

Checklist:
- task 1: arg passing & fpu (completed)
- task 2: process control syscall (in progress)
- task 3: file syscalls (complete)

Questions
- do I need to catch file NULL for fd (syscall.c)
- need validate # args (syscall.c)
- does exit(-1) calls need f->eax return values? 
 
 Solved questions 
 - do not need to allocate more space for fdt
 - pcb struct contains page_dir (process.h)
 - include filesys/filesys.h to fix circular dep (file.c, inode.c, syscall.c)
 - use putbuf (write to stdout), input_getc (get from stdin)

Note:
- c. files has .h files as dependency 
- build in files in /lib folder
- check valid user virt addr (vaddr.h), valid page (pagedir.h)
- num fd connected to inode in inode open_cnt (inode.c)

