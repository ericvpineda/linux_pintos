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
 
 Solved questions 
 - how to get page directory pd for pagedir_get_page -> pd in pcb (process.h)
 - fix circular dependency (file.c, inode.c, syscall.c)

Issues:

Log
- process.c - added fdt struct, fd_pos (current untaken fd)
- created filesys/misc_utils.c -> TEMP FIX: cyclic dependencies in syscall.c 
- created create() syscall (syscall.c)
- modify struct file to have name field (file.h)
- passing tests: all create test

Note:
- c. files has .h files as dependency 
- build in files in /lib folder
- check valid user virt addr (vaddr.h), valid page (pagedir.h)

