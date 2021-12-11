#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

/* ADDED: Total number of direct pointers in an on-disk inode */
#define TOTAL_DIRECT 12

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  off_t length; /* File size in bytes. (4 bytes) */
  int isdir;    // ADDED: 1 if inode is dir, 0 if is file (4 bytes)
  int files_rem; // ADDED: number of files remaining in directory if the inode is a directory (4 bytes)
  unsigned magic;                      /* Magic number. (4 bytes) */
  block_sector_t direct[TOTAL_DIRECT]; /* ADDED: direct pointers (12 * 4 = 36 bytes) */
  block_sector_t indirect;             /* ADDED: indirect pointer (4 bytes) */
  block_sector_t doubly_indirect;      /* ADDED: doubly indirect pointer (4 bytes) */
  uint32_t unused[110];                /* Not used. */
};

/* In-memory inode. */
struct inode {
  struct list_elem elem;  /* Element in inode list. */
  block_sector_t sector;  /* Sector number of disk location. */
  int open_cnt;           /* Number of openers. */
  bool removed;           /* True if deleted, false otherwise. */
  struct lock inode_lock; /* ADDED: Lock to synchronize operations on this struct. */

  struct lock deny_write_lock;    /* ADDED: Lock for deny writes. */
  struct condition deny_write_cv; /* ADDED: Conditional variable for deny writes. */
  int deny_write_cnt; /* 0: writes ok, >0: deny writes. | CHANGED: Number of current writers. */
  int deny_wait_cnt;  /* ADDED: Number of waiting writers. */
};

struct bitmap;
struct inode_disk;

bool inode_resize(struct inode_disk* id, block_sector_t id_sector, off_t size);
void inode_init(void);
bool inode_create(block_sector_t, off_t, int);
struct inode* inode_open(block_sector_t);
struct inode* inode_reopen(struct inode*);
block_sector_t inode_get_inumber(const struct inode*);
void inode_close(struct inode*);
void inode_remove(struct inode*);
block_sector_t inode_byte_to_sector(struct inode_disk* id, off_t pos);
off_t inode_read_at(struct inode*, void*, off_t size, off_t offset);
off_t inode_write_at(struct inode*, const void*, off_t size, off_t offset);
void inode_deny_write(struct inode*);
void inode_allow_write(struct inode*);
// off_t inode_length(const struct inode*);
bool inode_isdir(struct inode* inode);
struct inode_disk* get_inode_disk(struct inode* inode);
off_t inode_disk_length(const struct inode* inode);

#endif /* filesys/inode.h */
