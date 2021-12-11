#include "devices/block.h"
#include "filesys/off_t.h"

struct cache_item {
  int valid;     // keeps track of whether the item/entry is valid, 0 if invalid, 1 if valid
  int dirty_bit; // write-back cache so 1 if item has been modified, 0 if not
  int clock_bit; // clock algorithm recency bit; 0 if non-recent, 1 if recent
  int buffer
      [BLOCK_SECTOR_SIZE]; // buffer that contains data of the cache item; replacement for the bounce buffer
  block_sector_t sector; // sector number of disk location
};

void cache_init(void);
void cache_write(struct block* block, block_sector_t sector, void* buffer);
void cache_write_at(struct block* block, block_sector_t sector, void* buffer, off_t size,
                    off_t offset);
void cache_read(struct block* block, block_sector_t sector, void* buffer);
void cache_read_at(struct block* block, block_sector_t sector, void* buffer, off_t size,
                   off_t offset);
void cache_flush(void);
void clock_evict(struct block*, block_sector_t, void*, int, off_t, off_t);