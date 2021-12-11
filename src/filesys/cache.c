#include "filesys/cache.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <string.h>

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

void cache_write(struct block* block, block_sector_t sector, void* buffer) {
  struct block* fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device != block) {
    block_write(block, sector, buffer);
    return;
  }
  for (int i = 0; i < 64; i++) {
    if (sector == buffer_cache[i].sector) {
      lock_acquire(&sector_locks[i]);
      buffer_cache[i].dirty_bit = 1;
      buffer_cache[i].clock_bit = 1;
      memcpy(buffer_cache[i].buffer, buffer, BLOCK_SECTOR_SIZE);
      lock_release(&sector_locks[i]);
      return;
    }
  }
  clock_evict(fs_device, sector, buffer, 1);
  cache_flush();
}

void cache_read(struct block* block, block_sector_t sector, void* buffer) {
  struct block* fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device != block) {
    block_read(block, sector, buffer);
    return;
  }
  for (int i = 0; i < 64; i++) {
    if (sector == buffer_cache[i].sector) {
      lock_acquire(&sector_locks[i]);
      buffer_cache[i].clock_bit = 1;
      memcpy(buffer, buffer_cache[i].buffer, BLOCK_SECTOR_SIZE);
      lock_release(&sector_locks[i]);
      return;
    }
  }
  clock_evict(fs_device, sector, buffer, 0);
  cache_flush();
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

void clock_evict(struct block* fs_device, block_sector_t sector, void* buffer, int write) {
  while (true) {
    clock_hand += 1;
    int clock_index = clock_hand % 64;
    lock_acquire(&sector_locks[clock_index]);
    lock_acquire(&global_cache_lock);
    if (buffer_cache[clock_index].clock_bit == 0) {
      if (write) {
        buffer_cache[clock_index].valid = 1;
        buffer_cache[clock_index].dirty_bit = 1;
        buffer_cache[clock_index].clock_bit = 1;
        memcpy(buffer_cache[clock_index].buffer, buffer, BLOCK_SECTOR_SIZE);
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
        memcpy(buffer, buffer_cache[clock_index].buffer, BLOCK_SECTOR_SIZE);
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