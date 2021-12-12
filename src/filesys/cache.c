#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <string.h>
#include "filesys/off_t.h"

static struct cache_item buffer_cache[64];
static struct lock
    global_cache_lock;               // global cache lock for cache misses (compulsory and capacity)
static struct lock sector_locks[64]; // read/write sector locks to protect data for each sector
static int clock_hand;               // keeps track of the index of the clock hand

void cache_init(void) {
  lock_init(&global_cache_lock);
  for (int i = 0; i < 64; i++) {
    lock_init(&sector_locks[i]);
  }
  clock_hand = 0;
}

void cache_read_at(struct block* block, block_sector_t sector, void* buffer, off_t size,
                   off_t offset) {
  struct block* fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device != block) {
    block_read(block, sector, buffer);
    return;
  }
  for (int i = 0; i < 64; i++) {
    if (sector == buffer_cache[i].sector && buffer_cache[i].valid == 1) {
      lock_acquire(&sector_locks[i]);
      buffer_cache[i].clock_bit = 1;
      void* buf = buffer_cache[i].buffer;
      memcpy(buffer, buf + offset, size);
      lock_release(&sector_locks[i]);
      return;
    }
  }
  clock_evict(fs_device, sector, buffer, 0, size, offset);
}

void cache_write_at(struct block* block, block_sector_t sector, void* buffer, off_t size,
                    off_t offset) {
  struct block* fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device != block) {
    block_write(block, sector, buffer);
    return;
  }
  if (size == BLOCK_SECTOR_SIZE && offset == 0) {
    for (int i = 0; i < 100; i++) {
      if (((int*)buffer)[i] != 0) {
        goto temp;
      }
    }
    int a = 3;
  }
temp:
  for (int i = 0; i < 64; i++) {
    if (sector == buffer_cache[i].sector && buffer_cache[i].valid == 1) {
      lock_acquire(&sector_locks[i]);
      buffer_cache[i].dirty_bit = 1;
      buffer_cache[i].clock_bit = 1;
      void* buf = buffer_cache[i].buffer;
      memcpy(buf + offset, buffer, size);
      lock_release(&sector_locks[i]);
      return;
    }
  }
  clock_evict(fs_device, sector, buffer, 1, size, offset);
}

void cache_write(struct block* block, block_sector_t sector, void* buffer) {
  cache_write_at(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void cache_read(struct block* block, block_sector_t sector, void* buffer) {
  cache_read_at(block, sector, buffer, BLOCK_SECTOR_SIZE, 0);
}

void cache_flush(void) {
  struct block* fs_device = block_get_role(BLOCK_FILESYS);
  lock_acquire(&global_cache_lock);
  for (int i = 0; i < 64; i++) {
    if (buffer_cache[i].dirty_bit == 1) {
      block_write(fs_device, buffer_cache[i].sector, buffer_cache[i].buffer);
    }
  }
  lock_release(&global_cache_lock);
}

void clock_evict(struct block* fs_device, block_sector_t sector, void* buffer, int write,
                 off_t size, off_t offset) {
  while (true) {
    clock_hand += 1;
    int clock_index = clock_hand % 64;
    lock_acquire(&sector_locks[clock_index]);
    lock_acquire(&global_cache_lock);
    if (buffer_cache[clock_index].clock_bit == 0) {
      if (buffer_cache[clock_index].dirty_bit == 1) {
        block_write(fs_device, buffer_cache[clock_index].sector, buffer_cache[clock_index].buffer);
      }
      if (write) {
        buffer_cache[clock_index].valid = 1;
        buffer_cache[clock_index].dirty_bit = 1;
        buffer_cache[clock_index].clock_bit = 1;
        void* buf = buffer_cache[clock_index].buffer;
        block_read(fs_device, sector, buf);
        memcpy(buf + offset, buffer, size);
        buffer_cache[clock_index].sector = sector;
        lock_release(&sector_locks[clock_index]);
        lock_release(&global_cache_lock);
        return;
      } else {
        buffer_cache[clock_index].valid = 1;
        buffer_cache[clock_index].dirty_bit = 0;
        buffer_cache[clock_index].clock_bit = 1;
        buffer_cache[clock_index].sector = sector;
        block_read(fs_device, sector, buffer_cache[clock_index].buffer);
        void* buf = buffer_cache[clock_index].buffer;
        memcpy(buffer, buf + offset, size);
        lock_release(&sector_locks[clock_index]);
        lock_release(&global_cache_lock);
        return;
      }
    }
    buffer_cache[clock_index].clock_bit = 0;
    lock_release(&sector_locks[clock_index]);
    lock_release(&global_cache_lock);
  }
}