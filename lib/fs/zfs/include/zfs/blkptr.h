#pragma once

typedef struct {
  uint64_t part1;
  uint64_t offset;
} dva_t;

typedef struct {
  dva_t dva[3];
  uint64_t flags;
  /*
   *  0:24  lsize for embedded blk ptrs
   * 25:31  psize for embedded blk ptrs
   *  0:15  lsize for normal blk ptrs
   * 16:31  psize for normal blk ptrs
   * 32:39  compressed
   * 39:    blkptr has embedded data
   * 40:47  checksum type
   * 48:55  type
   * 56:60  level
   * 61     encryption
   * 62     dedup
   * 63     byte order
   * */
  uint64_t padding[3];
  uint64_t birth_txg;
  uint64_t fill_count;
  uint64_t checksum[4];
} blkptr_t;

void print_block_pointer(const blkptr_t *ptr);

static inline bool blkptr_is_embedded(const blkptr_t *ptr) {
  return (ptr->flags >> 39) & 1;
}

static inline int blkptr_get_embedded_lsize(const blkptr_t *ptr) {
  return (ptr->flags & 0xffffff) + 1;
}

static inline int blkptr_get_embedded_psize(const blkptr_t *ptr) {
  return ((ptr->flags >> 25) & 0x7f) + 1;
}

// the size of a record before compression
static inline int blkptr_get_lsize(const blkptr_t *ptr) {
  if (blkptr_is_embedded(ptr)) return blkptr_get_embedded_lsize(ptr);
  uint32_t lsize = ptr->flags & 0xffff;
  return (lsize + 1) << 9;
}

// the size of a record on-disk after compression
static inline int blkptr_get_psize(const blkptr_t *ptr) {
  uint32_t psize = (ptr->flags >> 16) & 0xffff;
  return (psize + 1) << 9;
}

static inline uint8_t blkptr_get_compression(const blkptr_t *ptr) {
  return (ptr->flags >> 32) & 0x7f;
}

static inline uint8_t blkptr_get_checksum_type(const blkptr_t *ptr) {
  return (ptr->flags >> 40) & 0xff;
}

static inline int blkptr_get_level(const blkptr_t *ptr) {
  return (ptr->flags >> 56) & 0x7f;
}

