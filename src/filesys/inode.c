#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. (make sure ';') */
#define INODE_MAGIC 0x494e4f44

/* ADDED: Number of pointers in buffers for indirect and doubly_indirect */
#define NUM_INDIRECT 128

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* Resizes inode on disk ID located at sector ID_SECTOR to length SIZE. */
bool inode_resize(struct inode_disk* id, block_sector_t id_sector, off_t size) {
  // Allocate buffers
  block_sector_t* buffer = (block_sector_t*)malloc(sizeof(block_sector_t) * NUM_INDIRECT);
  block_sector_t* buffer2 = (block_sector_t*)malloc(sizeof(block_sector_t) * NUM_INDIRECT);
  int* zero_block = (int*)calloc(1, BLOCK_SECTOR_SIZE);

  /* Direct pointers */
  for (int i = 0; i < TOTAL_DIRECT; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && id->direct[i] != 0) {
      // Free direct data blocks if needed
      free_map_release(id->direct[i], 1);
      id->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && id->direct[i] == 0) {
      // Allocate and zero out the new data block
      if (!free_map_allocate(1, &id->direct[i]))
        goto rollback;
      cache_write(fs_device, id->direct[i], zero_block);
    }
  }

  // Return if we don't need the indirect pointer
  if (id->indirect == 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    goto complete;
  }

  /* Indirect pointer */
  if (id->indirect == 0) {
    // Allocate block for indirect pointer if it doesn't exist and zero out the buffer
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    if (!free_map_allocate(1, &id->indirect))
      goto rollback;
  } else {
    // Read indirect pointer into buffer from disk
    cache_read(fs_device, id->indirect, buffer);
  }

  for (int i = 0; i < NUM_INDIRECT; i++) {
    if (size <= (TOTAL_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      // Free data blocks in indirect tree if needed
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else if (size > (TOTAL_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      // Allocate and zero out new data block
      if (!free_map_allocate(1, &buffer[i]))
        goto rollback;
      cache_write(fs_device, buffer[i], zero_block);
    }
  }
  if (id->indirect != 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    // Free indirect pointer if it is allocated and not needed
    free_map_release(id->indirect, 1);
    id->indirect = 0;
  } else {
    // Write indirect pointer tree to disk
    cache_write(fs_device, id->indirect, buffer);
  }

  // Return if we don't need the doubly indirect pointer
  if (id->doubly_indirect == 0 && size <= (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    goto complete;
  }

  /* Doubly indirect pointer */
  if (id->doubly_indirect == 0) {
    // Allocate block for doubly indirect pointer if it doesn't exist and zero out the buffer
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    if (!free_map_allocate(1, &id->doubly_indirect))
      goto rollback;
  } else {
    // Read doubly indirect pointer into buffer from disk
    cache_read(fs_device, id->doubly_indirect, buffer);
  }

  // Deallocate or allocate space if necessary
  for (int i = 0; i < NUM_INDIRECT; i++) {
    if (buffer[i] == 0) {
      memset(buffer2, 0, BLOCK_SECTOR_SIZE);
      if (size > (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT) * BLOCK_SECTOR_SIZE &&
          !free_map_allocate(1, &buffer[i]))
        goto rollback;
    } else {
      cache_read(fs_device, buffer[i], buffer2);
    }

    for (int j = 0; j < NUM_INDIRECT; j++) {
      if (size <= (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT + j) * BLOCK_SECTOR_SIZE &&
          buffer2[j] != 0) {
        free_map_release(buffer2[j], 1);
        buffer2[j] = 0;
      } else if (size > (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT + j) * BLOCK_SECTOR_SIZE &&
                 buffer2[j] == 0) {
        if (!free_map_allocate(1, &buffer2[j]))
          goto rollback;
        cache_write(fs_device, buffer2[j], zero_block);
      }
    }

    // Free indirect pointer if it is allocated and not needed
    if (buffer[i] != 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else {
      // Write indirect pointer tree to disk
      cache_write(fs_device, buffer[i], buffer2);
    }
    // cache_write(fs_device, id->doubly_indirect, buffer);
  }

  if (id->doubly_indirect != 0 && size <= (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    // Free doubly indirect pointer if it is allocated and not needed
    free_map_release(id->doubly_indirect, 1);
    id->doubly_indirect = 0;
  } else {
    // Write doubly indirect tree to disk
    cache_write(fs_device, id->doubly_indirect, buffer);
  }

complete:
  // TODO: release all locks
  free(buffer);
  free(buffer2);
  free(zero_block);
  id->length = size;
  cache_write(fs_device, id_sector, id);
  return true;

rollback:
  // TODO: release all locks
  free(buffer);
  free(buffer2);
  free(zero_block);
  inode_resize(id, id_sector, id->length);
  return false;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void) {
  lock_init(&open_inodes_lock);
  list_init(&open_inodes);
}

/* Resizes inode on disk ID located at sector ID_SECTOR to length SIZE. */
bool inode_resize(struct inode_disk* id, block_sector_t id_sector, off_t size) {
  // Allocate buffers
  block_sector_t* buffer = (block_sector_t*)malloc(sizeof(block_sector_t) * NUM_INDIRECT);
  block_sector_t* buffer2 = (block_sector_t*)malloc(sizeof(block_sector_t) * NUM_INDIRECT);
  int* zero_block = (int*)calloc(1, BLOCK_SECTOR_SIZE);

  /* Direct pointers */
  for (int i = 0; i < TOTAL_DIRECT; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && id->direct[i] != 0) {
      // Free direct data blocks if needed
      free_map_release(id->direct[i], 1);
      id->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && id->direct[i] == 0) {
      // Allocate and zero out the new data block
      if (!free_map_allocate(1, &id->direct[i]))
        goto rollback;
      cache_write(fs_device, id->direct[i], zero_block);
    }
  }

  // Return early if we don't need the indirect pointer and the tree doesn't need to be freed
  if (id->indirect == 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    goto complete;
  }

  /* Indirect pointer */
  if (id->indirect == 0) {
    // Allocate block for indirect pointer if it doesn't exist and zero out the buffer
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    if (!free_map_allocate(1, &id->indirect))
      goto rollback;
  } else {
    // Read indirect pointer into buffer from disk
    cache_read(fs_device, id->indirect, buffer);
  }

  for (int i = 0; i < NUM_INDIRECT; i++) {
    if (size <= (TOTAL_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      // Free data blocks in indirect tree if needed
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else if (size > (TOTAL_DIRECT + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      // Allocate and zero out new data block
      if (!free_map_allocate(1, &buffer[i]))
        goto rollback;
      cache_write(fs_device, buffer[i], zero_block);
    }
  }
  if (id->indirect != 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    // Free indirect pointer if it is allocated and not needed
    free_map_release(id->indirect, 1);
    id->indirect = 0;
  } else {
    // Write indirect pointer tree to disk
    cache_write(fs_device, id->indirect, buffer);
  }

  // Return early if we don't need the doubly indirect pointer and the tree doesn't need to be freed
  if (id->doubly_indirect == 0 && size <= (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    goto complete;
  }

  /* Doubly indirect pointer */
  if (id->doubly_indirect == 0) {
    // Allocate block for doubly indirect pointer if it doesn't exist and zero out the buffer
    memset(buffer, 0, BLOCK_SECTOR_SIZE);
    if (!free_map_allocate(1, &id->doubly_indirect))
      goto rollback;
  } else {
    // Read doubly indirect pointer into buffer from disk
    cache_read(fs_device, id->doubly_indirect, buffer);
  }

  // Deallocate or allocate space if necessary
  for (int i = 0; i < NUM_INDIRECT; i++) {
    if (buffer[i] == 0) {
      memset(buffer2, 0, BLOCK_SECTOR_SIZE);
      if (size > (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT) * BLOCK_SECTOR_SIZE &&
         !free_map_allocate(1, &buffer[i]))
        goto rollback;
    } else {
      cache_read(fs_device, buffer[i], buffer2);
    }

    for (int j = 0; j < NUM_INDIRECT; j++) {
      if (size <= (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT + j) * BLOCK_SECTOR_SIZE &&
          buffer2[j] != 0) {
        free_map_release(buffer2[j], 1);
        buffer2[j] = 0;
      }
      else if (size > (TOTAL_DIRECT + NUM_INDIRECT + i * NUM_INDIRECT + j) * BLOCK_SECTOR_SIZE &&
               buffer2[j] == 0) {
        if (!free_map_allocate(1, &buffer2[j]))
          goto rollback;
        cache_write(fs_device, buffer2[j], zero_block);
      }
    }

    // Free indirect pointer if it is allocated and not needed
    if (buffer[i] != 0 && size <= TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
      free_map_release(buffer[i], 1);
      buffer[i] = 0;
    } else {
      // Write indirect pointer tree to disk
      cache_write(fs_device, buffer[i], buffer2);
    }
    // cache_write(fs_device, id->doubly_indirect, buffer);
  }

  if (id->doubly_indirect != 0 && size <= (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    // Free doubly indirect pointer if it is allocated and not needed
    free_map_release(id->doubly_indirect, 1);
    id->doubly_indirect = 0;
  } else {
    // Write doubly indirect tree to disk
    cache_write(fs_device, id->doubly_indirect, buffer);
  }

complete:
  // TODO: release all locks
  free(buffer);
  free(buffer2);
  free(zero_block);
  id->length = size;
  cache_write(fs_device, id_sector, id);
  return true;

rollback:
  // TODO: release all locks
  free(buffer);
  free(buffer2);
  free(zero_block);
  inode_resize(id, id_sector, id->length);
  return false;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. 
   Note: free_map_allocate called on inode_disk in parent function(s)
*/

bool inode_create(block_sector_t sector, off_t length, int isdir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    // Returns number of sectors to allocate
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->isdir = isdir;

    for (int i = 0; i < TOTAL_DIRECT; i++)
      disk_inode->direct[i] = 0;
    disk_inode->indirect = 0;
    disk_inode->doubly_indirect = 0;

    // Resize inode to length
    success = inode_resize(disk_inode, sector, length);

    // Free disk_inode since calloced in this function
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      // Updates inode open count
      inode_reopen(inode);
      return inode;
    }
  }
  lock_release(&open_inodes_lock);

  /* Allocate memory for inode. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  // TODO: synchronize this operation (piazza: create static lock for open_inodes)
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->deny_wait_cnt = 0;
  inode->removed = false;
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->inode_lock);
    inode->open_cnt++;
    lock_release(&inode->inode_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)id);

  /* Release resources if this was the last opener. */
  lock_acquire(&inode->inode_lock);
  if (--inode->open_cnt == 0) {
    lock_release(&inode->inode_lock);

    /* Remove from inode list and release lock. */
    // TODO: synchronize this operation
    list_remove(&inode->elem);
    lock_release(&open_inodes_lock);

    /* Deallocate blocks if removed. */
    lock_acquire(&inode->inode_lock);
    if (inode->removed) {
      // Free all direct pointers
      for (int i = 0; i < TOTAL_DIRECT; i++) {
        if (id->direct[i] != 0)
          free_map_release(id->direct[i], 1);
      }

      // Free the indirect pointer tree
      block_sector_t buffer[NUM_INDIRECT];
      if (id->indirect != 0) {
        cache_read(fs_device, id->indirect, buffer);
        for (int i = 0; i < NUM_INDIRECT; i++) {
          if (buffer[i] != 0)
            free_map_release(buffer[i], 1);
        }
        free_map_release(id->indirect, 1);
      }

      // Free the doubly indirect tree
      block_sector_t buffer2[NUM_INDIRECT];
      if (id->doubly_indirect != 0) {
        cache_read(fs_device, id->doubly_indirect, buffer);
        for (int i = 0; i < NUM_INDIRECT; i++) {
          if (buffer[i] == 0)
            continue;
          cache_read(fs_device, buffer[i], buffer2);
          for (int j = 0; j < NUM_INDIRECT; j++) {
            if (buffer2[j] != 0)
              free_map_release(buffer2[j], 1);
          }
          free_map_release(buffer[i], 1);
        }
        free_map_release(id->doubly_indirect, 1);
      }

      // Free the inode_disk
      free_map_release(inode->sector, 1);
    }
    free(inode);
  } else {
    lock_release(&inode->inode_lock);
  }
  free(id);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->inode_lock);
  inode->removed = true;
  lock_release(&inode->inode_lock);
}

/* Returns the block device sector that contains byte offset POS
   within inode disk ID.
   Returns -1 if INODE does not contain data for a byte at offset
   POS or if the desired block sector has not been allocated. */
block_sector_t inode_byte_to_sector(struct inode_disk* id, off_t pos) {
  if (pos >= id->length)
    return (block_sector_t) -1;

  block_sector_t buffer[NUM_INDIRECT];

  // Direct case
  if (pos < TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    int sector_idx = pos / BLOCK_SECTOR_SIZE;
    return id->direct[sector_idx] != 0 ? id->direct[sector_idx] : (block_sector_t) -1;
    // return id->direct[sector_idx];
  }
  // Indirect case
  else if (pos < (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    if (id->indirect == 0)
      return -1;
    cache_read(fs_device, id->indirect, buffer);
    int sector_idx = (pos - TOTAL_DIRECT * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;
    return buffer[sector_idx] != 0 ? buffer[sector_idx] : (block_sector_t) -1;
    // return buffer[sector_idx];
  }
  // Doubly indirect case
  else {
    // Q: might not need these if cases bc the only unallocated blocks should be those past id->length
    if (id->doubly_indirect == 0)
      return -1;
    cache_read(fs_device, id->doubly_indirect, buffer);
    int indirect_idx = (pos - (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) / (BLOCK_SECTOR_SIZE * NUM_INDIRECT);
    if (buffer[indirect_idx] == 0)
      return -1;
    block_sector_t buffer2[NUM_INDIRECT];
    cache_read(fs_device, buffer[indirect_idx], buffer2);
    int direct_idx = (pos - (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;

    return buffer2[direct_idx] != 0 ? buffer2[direct_idx] : (block_sector_t) -1;
    // return buffer2[direct_idx];
  }
}

/* Returns the block device sector that contains byte offset POS
   within inode disk ID.
   Returns -1 if INODE does not contain data for a byte at offset
   POS or if the desired block sector has not been allocated. */
block_sector_t inode_byte_to_sector(struct inode_disk* id, off_t pos) {
  if (pos >= id->length)
    return -1;

  block_sector_t buffer[NUM_INDIRECT];

  // Direct case
  if (pos < TOTAL_DIRECT * BLOCK_SECTOR_SIZE) {
    int sector_idx = pos / BLOCK_SECTOR_SIZE;
    return id->direct[sector_idx] != 0 ? id->direct[sector_idx] : -1;
    // return id->direct[sector_idx];
  }
  // Indirect case
  else if (pos < (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) {
    if (id->indirect == 0)
      return -1;
    cache_read(fs_device, id->indirect, buffer);
    int sector_idx = (pos - TOTAL_DIRECT * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;
    return buffer[sector_idx] != 0 ? buffer[sector_idx] : -1;
    // return buffer[sector_idx];
  }
  // Doubly indirect case
  else {
    // Q: might not need these if cases bc the only unallocated blocks should be those past id->length
    if (id->doubly_indirect == 0)
      return -1;
    cache_read(fs_device, id->doubly_indirect, buffer);
    int indirect_idx = (pos - (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) /
                       (BLOCK_SECTOR_SIZE * NUM_INDIRECT);
    if (buffer[indirect_idx] == 0)
      return -1;
    block_sector_t buffer2[NUM_INDIRECT];
    cache_read(fs_device, buffer[indirect_idx], buffer2);
    int direct_idx = (pos - (TOTAL_DIRECT + NUM_INDIRECT) * BLOCK_SECTOR_SIZE) / BLOCK_SECTOR_SIZE;

    return buffer2[direct_idx] != 0 ? buffer2[direct_idx] : -1;
    // return buffer2[direct_idx];
  }
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)id);

  /* Return 0 if offset is past EOF */
  if (offset + size > id->length) {
    free(id);
    return 0;
  }

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = inode_byte_to_sector(id, offset);
    if (sector_idx == -1) {
      free(id);
      return 0;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_disk_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      cache_read(fs_device, sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */

      // NOTE: change later to use buffer cache
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      cache_read(fs_device, sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);
  free(id);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;

  /* Variables 
    - lock_cv
    - cv
    - deny_write_cnt (already have) [waiting]
    - active_write_cnt [num active threads modifying the file] -- probably don't need??? should be at most 1
  */

  /* Functions to modify 
    - inode_write_at
    - inode_deny_write? 
  */

  /* Q's to ask Edward:
    - Go over current synch implementation for use of inode_lock to see if what we have is correct
    - do we need to synchronize cache_read/cache_writes? (unless sync happens in buffer cache?)
    - what is deny_write_cnt meant to do? (does it handle synchronization between reads and writes or only concurrent writes?)
      - if the answer is the latter then how are we supposed to handle synchronization between reads and writes like in 1.4 from the 
        spec bc gradescope doesn't seem to explicitly mention it
    - are we supposed to call inode_deny_write/inode_allow_write inside of inode_write_at/inode_read_at?
    - do we need a CV to synchronize deny_write_cnt, or can use use a per-inode lock instead? (i.e. inode->inode_lock)
    - CV details:
      - can we have some general guidance/overall tips on how to approach the cv implementation?
      - is using a rw_lock feasible: no
  */

  // lock_acquire(&inode->deny_write_lock);
  if (inode->deny_write_cnt) {
    // lock_release(&inode->deny_write_lock);
    return 0;
  }
  // lock_release(&inode->deny_write_lock);

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)id);

  // Need to implement conditional var
  // lock_acquire(&inode->deny_write_lock);

  // while (inode->deny_write_cnt > 0) {
  //   inode->deny_wait_cnt++;
  //   cond_wait(&inode->deny_write_cv, &inode->deny_write_lock);
  //   inode->deny_wait_cnt--;
  // }

  // lock_release(&inode->deny_write_lock);

  /* Extend the file if the offset is greater than the current inode_disk length */
  if (offset + size > id->length) {
    lock_acquire(&inode->inode_lock);
    if (!inode_resize(id, inode->sector, offset + size)) {
      lock_release(&inode->inode_lock);
      free(id);
      return 0;
    }
    lock_release(&inode->inode_lock);
  }

  struct inode_disk* id = (struct inode_disk*)malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, (void*)id);

  /* Extend the file if the offset is greater than the current inode_disk length */
  if (offset + size > id->length) {
    if (!inode_resize(id, inode->sector, offset + size)) {
      free(id);
      return 0;
    }
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = inode_byte_to_sector(id, offset);
    // TEMP: delete this if statement later
    if (sector_idx == -1) {
      // THIS SHOULD NEVER OCCUR
      free(id);
      return -1;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_disk_length(inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      cache_write(fs_device, sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        cache_read(fs_device, sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      cache_write(fs_device, sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);
  free(id);

  return bytes_written;
}

// TODO: something about a CV for inode_deny_write and inode_allow_write
/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  // TODO: change to CV so that writes get priority over inode_deny_write()
  // lock_acquire(&inode->deny_write_lock);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  // lock_release(&inode->deny_write_lock);

  // lock_acquire(&inode->deny_write_lock);
  // while (inode->deny_write_cnt > 0) {
  //   inode->deny_wait_cnt++;
  //   cond_wait(&inode->deny_write_cv, &inode->deny_write_lock);
  //   inode->deny_wait_cnt--;
  // }
  // inode->deny_write_cnt++;
  // ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  // lock_release(&inode->deny_write_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  // lock_acquire(&inode->deny_write_lock);
  // ASSERT(inode->deny_write_cnt > 0);
  // ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  // inode->deny_write_cnt--;

  // if (inode->deny_write_cnt == 0)
  //   cond_signal(&inode->deny_write_cv, &inode->deny_write_lock);
  // lock_release(&inode->deny_write_lock);

  // lock_acquire(&inode->deny_write_lock);
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  // lock_acquire(&inode->deny_write_lock);
}

/* Returns true if inode is a directory, false if inode is a file. */
bool inode_isdir(struct inode* inode) {
  struct inode_disk* id = get_inode_disk(inode);
  bool res = id->isdir;
  free(id);
  return res;
}

// Free everytime you call this function
struct inode_disk* get_inode_disk(struct inode* inode) {
  struct inode_disk* id = malloc(sizeof(struct inode_disk));
  cache_read(fs_device, inode->sector, id);
  return id;
}
/* Returns the length, in bytes, of INODE_DISK's data. */
off_t inode_disk_length(const struct inode* inode) {
  struct inode_disk id;
  cache_read(fs_device, inode->sector, (void*)&id);
  return id.length;
}

