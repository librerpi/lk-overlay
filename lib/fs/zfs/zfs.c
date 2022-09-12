#include <app.h>
#include <endian.h>
#include <lib/bio.h>
#include <lib/fs.h>
#include <lib/hexdump.h>
#include <lk/err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lz4.h>

typedef struct {
  uint32_t total_length;
  uint32_t something;
  uint32_t name_length;
  char name[0];
} packed_nvpair_start;

typedef struct {
  uint32_t type;
  uint32_t something;
  union {
    uint64_t v;
    struct {
      uint32_t length;
      char value[0];
    } str;
  } value;
} packed_nvpair_middle;

typedef struct {
  uint64_t part1;
  uint64_t offset;
} dva_t;

typedef struct {
  dva_t dva[3];
  uint64_t flags;
  uint64_t padding[3];
  uint64_t birth_txg;
  uint64_t fill_count;
  uint64_t checksum[4];
} blkptr_t;

typedef struct {
  uint8_t dn_type;        // the type of the object this dnode points to
  uint8_t dn_indblkshift; // each indirect block is 2^dn_indblkshift bytes long
  uint8_t dn_nlevels;     // how many levels of indirection
  uint8_t dn_nblkptr;     // how many elements long dn_blkptr is

  uint8_t dn_bonustype;
  uint8_t dn_checksum;
  uint8_t dn_compress;
  uint8_t dn_pad[1];

  uint16_t dn_datablkszsec; // BE16, dblk(from zdb) == dn_datablkszsec * 512
  uint16_t dn_bonuslen;

  uint8_t dn_pad2[4];

  uint64_t dn_maxblkid;     // dnode maxblkid from zdb, to limit what range of L0's are valid
  uint64_t dn_secphys;      // sum of asize for all block pointers (data and indirect), zdb calls it dsize
  uint64_t dn_pad3[4];
  blkptr_t dn_blkptr[3];
  uint8_t  dn_padding[64];
} dnode_phys_t;

typedef struct {
  bdev_t *dev;
  uint64_t version;
  char *name;
  uint64_t guid;
  uint64_t top_guid;
  uint64_t pool_guid;
  uint64_t ashift;
  uint64_t vdev_children;
  dnode_phys_t mos_dnode;
} pool_t;

typedef struct {
  uint64_t ub_magic;
  uint64_t ub_version;
  uint64_t ub_txg;
  uint64_t ub_guid_sum; // sum of every vdev guid in the pool
  uint64_t ub_timestamp;
  blkptr_t ub_rootbp;   // pointer to the MOS
} uberblock_t;

typedef struct {
  int type;
  union {
    uint64_t uint64;
    char *str;
    struct { void *start; uint32_t length; } blob;
  } v;
} value_t;

void vdev_nvlist_callback(void *cookie, const char *name, value_t *val);
void print_block_pointer(const blkptr_t *ptr);

uint64_t nvpair_get_uint64(packed_nvpair_middle *val) {
  assert(BE32(val->type) == 8);
  return BE64(val->value.v);
}

void nvpair_get_string(packed_nvpair_middle *val, char **name) {
  assert(BE32(val->type) == 9);
  uint32_t len = BE32(val->value.str.length);
  *name = malloc(len);
  memcpy(*name, val->value.str.value, len);
  (*name)[len] = 0;
}

bool parse_nvlist(void* cookie, void *start, uint32_t size, void (*callback)(void*, const char*, value_t *)) {
  if (BE64(*((uint64_t*)start)) != 1) return false;
  void *ptr = start + 8;
  value_t outval;
  while (true) {
    uint32_t length = BE32(*((uint32_t*)ptr));
    //printf("ptr = 0x%lx, length: %d/0x%x, ",  ((void*)ptr) - start, length, length);
    if (length == 0) break;
    packed_nvpair_start *nv_start = ptr;
    uint32_t name_length = BE32(nv_start->name_length);
    //printf("something: 0x%x, name_length: %d, ", BE32(nv_start->something), name_length);
    char name[128];
    memcpy(name, &nv_start->name, MIN(name_length, 127));
    name[MIN(name_length, 127)] = 0;
    //printf("name: '%s', ", name);
    void *value_start = ((void*)ptr) + 12 + ROUNDUP(name_length,4);
    packed_nvpair_middle *mid = value_start;
    outval.type = BE32(mid->type);
    //printf("type: %d\n", outval.type);
    switch (outval.type) {
    case 8:
    {
      outval.v.uint64 = BE64(mid->value.v);
      printf("%s == %llu / 0x%llx\n", name, outval.v.uint64, outval.v.uint64);
      callback(cookie, name, &outval);
      break;
    }
    case 9:
    {
      nvpair_get_string(mid, &outval.v.str);
      printf("%s = %s\n", name, outval.v.str);
      callback(cookie, name, &outval);
      break;
    }
    case 19:
    {
      outval.v.blob.start = &mid->value;
      outval.v.blob.length = length - (outval.v.blob.start - ptr);
      printf("%s = nvlist(%p, %d)\n", name, outval.v.blob.start, outval.v.blob.length);
      callback(cookie, name, &outval);
      break;
    }
    default:
      printf("%s == ???\n", name);
    }

    ptr = ((void*)ptr) + length;
  }
  return true;
}

void vdev_root_nvlist_callback(void *cookie, const char *name, value_t *val) {
  pool_t *pool = cookie;
  if (strcmp(name, "version") == 0) {
    assert(val->type == 8);
    pool->version = val->v.uint64;
  } else if (strcmp(name, "name") == 0) {
    assert(val->type == 9);
    pool->name = strdup(val->v.str);
  } else if (strcmp(name, "top_guid") == 0) {
    assert(val->type == 8);
    pool->top_guid = val->v.uint64;
  } else if (strcmp(name, "guid") == 0) {
    assert(val->type == 8);
    pool->guid = val->v.uint64;
  } else if (strcmp(name, "pool_guid") == 0) {
    assert(val->type == 8);
    pool->pool_guid = val->v.uint64;
  } else if (strcmp(name, "vdev_children") == 0) {
    assert(val->type == 8);
    pool->vdev_children = val->v.uint64;
  } else if (strcmp(name, "vdev_tree") == 0) {
    assert(val->type == 19);
    parse_nvlist(cookie, val->v.blob.start, val->v.blob.length, vdev_nvlist_callback);
  }
}

void vdev_nvlist_callback(void *cookie, const char *name, value_t *val) {
  pool_t *pool = cookie;
  if (strcmp(name, "ashift") == 0) {
    assert(val->type == 8);
    pool->ashift = val->v.uint64;
  }
}

void uber_swap(uberblock_t *uber) {
  if (uber->ub_magic == 0xbab10c) {
    return;
  } else if (BE64(uber->ub_magic) == 0xbab10c) {
    BE64SWAP(uber->ub_magic);
    BE64SWAP(uber->ub_version);
    BE64SWAP(uber->ub_txg);
    BE64SWAP(uber->ub_guid_sum);
    BE64SWAP(uber->ub_timestamp);
  }
}

static inline int dnode_get_data_block_size(const dnode_phys_t *dnode) {
  return dnode->dn_datablkszsec * 512;
}

void print_dva(int dvanr, uint64_t part1, uint64_t offset) {
  printf("DVA[%d]=<%d:%llx:%x>", dvanr, (uint32_t)((part1 >> 32) & 0xffffffff), (offset & ~(((uint64_t)1) << 63)) << 9, (uint32_t)(part1 & 0xffffff) << 9);
}

const char *lookup_object_type(uint8_t type) {
  switch (type) {
  case 1:
    return "object directory";
  case 10:
    return "DMU dnode";
  case 11:
    return "DMU objset";
  }
  return "UNK";
}

void print_dnode(const dnode_phys_t *dn) {
  printf("dnode\n");
  printf("type: %s(%d)\n", lookup_object_type(dn->dn_type), dn->dn_type);
  printf("indirect shift: 2^%d == %d\n", dn->dn_indblkshift, 1 << dn->dn_indblkshift);
  printf("levels: %d\n", dn->dn_nlevels);
  printf("block ptr#: %d\n", dn->dn_nblkptr);

  printf("bonus type: %d, length: %d\n", dn->dn_bonustype, dn->dn_bonuslen);
  printf("checksum: %d\n", dn->dn_checksum);

  printf("block size: 512*%d == %d\n", dn->dn_datablkszsec, dnode_get_data_block_size(dn));

  printf("max block#: %lld\n", dn->dn_maxblkid);
  printf("sum(asize): %lld\n", dn->dn_secphys);
  for (int i=0; i<1; i++) {
    printf("dnode block ptr ");
    print_block_pointer(&dn->dn_blkptr[i]);
  }
}

const char *lookup_checksum(uint32_t algo) {
  switch (algo) {
  case 7:
    return "fletcher4";
  }
  return "UNK";
}

//int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize)
void zfs_lz4_decompress_record(void *input, int insize, void *output, int outsize) {
  uint32_t payload = BE32(*((uint32_t*)input));
  int ret = LZ4_decompress_safe(input+4, output, payload, outsize);
  printf("decompressed to 0x%x bytes\n", ret);
}

void decompress_record(void *input, int insize, void *output, int outsize, int algo) {
  switch (algo) {
  case 15:
    zfs_lz4_decompress_record(input, insize, output, outsize);
  }
}

void read_record(pool_t *pool, const blkptr_t *ptr, void *buffer, uint32_t size) {
  uint32_t psize = (((ptr->flags >> 16) & 0xffff) + 1) << 9;
  uint64_t offset = (ptr->dva[0].offset & ~(((uint64_t)1) << 63)) << 9;
  void *input = malloc(psize);
  printf("psize: 0x%x\n", psize);
  printf("offset: 0x%llx\n", offset);
  bio_read(pool->dev, input, (4 * 1024 * 1024) + offset, psize);
  uint8_t compression = (ptr->flags >> 32) & 0xff;
  decompress_record(input, psize, buffer, size, compression);
  free(input);
}

static inline int blkptr_get_level(const blkptr_t *ptr) {
  return (ptr->flags >> 56) & 0x7f;
}

void print_block_pointer(const blkptr_t *ptr) {
  //printf("flags: %llx\n", ptr->flags);
  assert(ptr->flags & ((uint64_t)1<<63)); // the LE flag
  for (int i=0; i<3; i++) {
    print_dva(i, ptr->dva[i].part1, ptr->dva[i].offset);
    printf(" ");
  }
  uint8_t type = (ptr->flags >> 48) & 0xff;
  uint8_t level = blkptr_get_level(ptr);
  printf("[L%d %s(%d)] ", level, lookup_object_type(type), type);
  uint32_t checksum_algo = (ptr->flags >> 40) & 0xff;
  printf("%s(%d) ", lookup_checksum(checksum_algo), checksum_algo);
  uint8_t compression = (ptr->flags >> 32) & 0xff;
  printf("comp=%d ", compression);
  uint32_t lsize = ptr->flags & 0xffff;
  uint32_t psize = (ptr->flags >> 16) & 0xffff;
  printf("size=0x%xL/0x%xP ", (lsize + 1) << 9, (psize + 1) << 9);
  printf("birth=%lld ", ptr->birth_txg);
  printf("cksum=%llx:%llx:%llx:%llx", ptr->checksum[0], ptr->checksum[1], ptr->checksum[2], ptr->checksum[3]);
  puts("");
}

void uber_print(uberblock_t *uber) {
  if (uber->ub_magic == 0xbab10c) {
    printf("\tmagic = 0x%llx\n", uber->ub_magic);
    printf("\tversion = %lld\n", uber->ub_version);
    printf("\ttxg: %lld\n", uber->ub_txg);
    printf("\tguid_sum: 0x%llx\n", uber->ub_guid_sum);
    printf("\ttimestamp: %lld\n", uber->ub_timestamp);
    printf("\tuberblock root block ptr ");
    print_block_pointer(&uber->ub_rootbp);
  }
}

void load_uberblock(pool_t *pool, int ubnum, uberblock_t *uber) {
  int stride = MAX(1024,1 << pool->ashift);
  int ret;
  ret = bio_read(pool->dev, uber, (128 * 1024) + (stride * ubnum), sizeof(uberblock_t));
  uber_swap(uber);
  assert(ret == sizeof(uberblock_t));
}

void dnode_load_block(pool_t *pool, const dnode_phys_t *dn, unsigned int blocknr, void *buffer, int size) {
  unsigned int indirect_block_size = 1 << dn->dn_indblkshift;
  unsigned int indirect_pointers_per_block = indirect_block_size / 128;
  printf("levels: %d\n", dn->dn_nlevels);
  printf("each indirection block holds %d pointers\n", indirect_pointers_per_block);
  blkptr_t ptr = dn->dn_blkptr[0];
  for (int i=dn->dn_nlevels-1; i > 0; i--) {
    int div_factor = indirect_pointers_per_block << (i-1);
    unsigned int Ln_index = blocknr / div_factor;
    printf("L%d index %d\n", i, Ln_index);
    void *iblk = malloc(indirect_block_size);
    read_record(pool, &ptr, iblk, indirect_block_size);
    //hexdump_ram(iblk, 0, 128 * 3);
    ptr = *(blkptr_t*)(iblk + (Ln_index * 128));
    free(iblk);
    assert(i <= 1); // L2 blocks untested
  }
  print_block_pointer(&ptr);
  assert(blkptr_get_level(&ptr) == 0);
  read_record(pool, &ptr, buffer, size);
}

void load_dnode_by_object_id(pool_t *pool, const dnode_phys_t *dnode_index, uint64_t objectid, dnode_phys_t *dnode_out) {
  const int dnode_size = 512; // TODO, where can i query this?
  int dnodes_per_block = dnode_get_data_block_size(dnode_index) / dnode_size;
  unsigned int blocknr = objectid / dnodes_per_block;
  assert(blocknr <= dnode_index->dn_maxblkid);
  int index = objectid % dnodes_per_block;
  printf("object %lld is in block %d, index %d * %d\n", objectid, blocknr, dnode_size, index);
  void *block = malloc(dnode_get_data_block_size(dnode_index));
  dnode_load_block(pool, dnode_index, blocknr, block, dnode_get_data_block_size(dnode_index));
  *dnode_out = *(dnode_phys_t*)(block + (dnode_size * index));
  free(block);
}

status_t zfs_mount(bdev_t *dev, fscookie **cookie) {
  int ret;

  if (!dev) return ERR_NOT_FOUND;
  void *buffer = malloc(112 * 1024);
  if (!buffer) return ERR_NO_MEMORY;

  ret = bio_read(dev, buffer, 16 * 1024, 112 * 1024);
  if (ret < 0) goto err;
  hexdump_ram(buffer, 16 * 1024, 1024);

  pool_t *pool = malloc(sizeof(pool_t));
  pool->dev = dev;
  pool->name = NULL;

  parse_nvlist(pool, buffer + 4, (112*1024)-4, vdev_root_nvlist_callback);
  if (pool->top_guid != pool->guid) {
    printf("mirror/raidz not supported\n");
    ret = ERR_NOT_SUPPORTED;
    goto err;
  }
  if (pool->vdev_children != 1) {
    printf("error, this pool is made up of %lld vdev's, but the code only supports 1\n", pool->vdev_children);
    ret = ERR_NOT_SUPPORTED;
    goto err;
  }
  puts("all parsing done");
  printf("version: %lld\nname: %s\npool_guid: %llx\nguid: %llx\nashift: 2^%lld == %d\n", pool->version, pool->name, pool->pool_guid, pool->guid, pool->ashift, 1 << pool->ashift);

  uint64_t latest_txg = 0;
  int latest_ub = 0;
  for (int i=0; i<128; i++) {
    uberblock_t uber;
    load_uberblock(pool, i, &uber);
    if (uber.ub_magic == 0xbab10c) {
      printf("Uberblock[%d]\n", i);
      uber_print(&uber);
      if (uber.ub_txg > latest_txg) {
        latest_txg = uber.ub_txg;
        latest_ub = i;
      }
    }
  }
  printf("using txg %lld and uberblock %d\n", latest_txg, latest_ub);
  printf("dnode size: %ld\n", sizeof(dnode_phys_t));
  uberblock_t uber;
  load_uberblock(pool, latest_ub, &uber);
  read_record(pool, &uber.ub_rootbp, &pool->mos_dnode, sizeof(pool->mos_dnode));
  hexdump_ram(&pool->mos_dnode, 0, 0xc0);
  print_dnode(&pool->mos_dnode);

  dnode_phys_t object1;
  load_dnode_by_object_id(pool, &pool->mos_dnode, 1, &object1);
  print_dnode(&object1);
  print_block_pointer(&object1.dn_blkptr[1]);

  return -1;
err:
  free(buffer);
  return ret;
}

static const struct fs_api zfs_api = {
  .mount = zfs_mount,
};

STATIC_FS_IMPL(zfs, &zfs_api);

static void zfs_entry(const struct app_descriptor *app, void *args) {
  int ret;
  ret = fs_mount("/lk", "zfs", "virtio0");
  if (ret) {
    printf("mount failure: %d\n", ret);
  }
}

APP_START(zfs_test)
  .entry = zfs_entry
APP_END

