#include <app.h>
#include <cksum-helper/cksum-helper.h>
#include <endian.h>
#include <lib/bio.h>
#include <lib/fs.h>
#include <lib/hexdump.h>
#include <lk/err.h>
#include <lz4.h>
#include <math.h>
#include <platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zfs/blkptr.h>

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
  uint64_t zh_claim_txg;  /* txg in which log blocks were claimed */
  uint64_t zh_replay_seq; /* highest replayed sequence number */
  blkptr_t zh_log;        /* log chain */
  uint64_t zh_claim_blk_seq; /* highest claimed block sequence number */
  uint64_t zh_flags;      /* header flags */
  uint64_t zh_claim_lr_seq; /* highest claimed lr sequence number */
  uint64_t zh_pad[3];
} zil_header_t;

typedef struct {
  uint64_t lrc_txtype;
  uint64_t lrc_reclen;
  uint64_t lrc_txg;
  uint64_t lrc_seq;
} lr_t;

typedef struct {
  uint64_t zec_magic;
  uint64_t checksum[4];
} zio_eck_t;

// zilog2 blocks have a zil_chain_t at the head
typedef struct {
  uint64_t zc_pad;
  blkptr_t zc_next_blk;
  uint64_t zc_nused;
  zio_eck_t zc_eck;
} zil_chain_t;

typedef struct {
  dnode_phys_t os_meta_dnode;
  zil_header_t os_zil_header;
} objset_phys_t;

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
void hexdump_record(pool_t *pool, const blkptr_t *ptr);
void read_record(pool_t *pool, const blkptr_t *ptr, void *buffer, uint32_t size);

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
  if (type & 0x80) {
    bool metadata = type & 0x40;
    bool encrypted = type & 0x20;
    uint8_t flag = type & 0x1f;
    if ((metadata) && (!encrypted) && (flag == 4)) return "zap";
  }
  switch (type) {
  case 1:
    return "object directory";
  case 10:
    return "DMU dnode";
  case 11:
    return "DMU objset";
  case 12:
    return "DSL directory";
  case 13:
    return "DSL directory child map";
  case 16:
    return "DSL dataset";
  }
  return "UNK";
}

void print_dnode(const dnode_phys_t *dn) {
  printf("PRINT_DNODE\n");
  printf("type: %s(%d)\n", lookup_object_type(dn->dn_type), dn->dn_type);
  printf("indirect shift: 2^%d == %d\n", dn->dn_indblkshift, 1 << dn->dn_indblkshift);
  printf("levels: %d\n", dn->dn_nlevels);
  printf("block ptr#: %d\n", dn->dn_nblkptr);

  printf("bonus type: %s(%d), length: %d\n", lookup_object_type(dn->dn_bonustype), dn->dn_bonustype, dn->dn_bonuslen);
  printf("checksum: %d\n", dn->dn_checksum);

  printf("block size: 512*%d == %d\n", dn->dn_datablkszsec, dnode_get_data_block_size(dn));

  printf("max block#: %lld\n", dn->dn_maxblkid);
  printf("sum(asize): %lld\n", dn->dn_secphys);
  for (int i=0; i<dn->dn_nblkptr; i++) {
    if ((dn->dn_nlevels > 1) && (i > 0)) break;
    printf("dnode->dn_blkptr[%d] ", i);
    print_block_pointer(&dn->dn_blkptr[i]);
  }
}

void print_zil_record(const lr_t *lr) {
  printf("lrc_txtype: %llx\n", lr->lrc_txtype);
  printf("lrc_reclen: %llx\n", lr->lrc_reclen);
  printf("lrc_txg:    %llx\n", lr->lrc_txg);
  printf("lrc_seq:    %llx\n", lr->lrc_seq);
}

void print_zil_chain(const zil_chain_t *c) {
  printf("zc_pad:   %lld\n", c->zc_pad);
  print_block_pointer(&c->zc_next_blk);
  printf("zc_nused: %lld\n", c->zc_nused);
}

void print_zil_header(pool_t *pool, const zil_header_t *zh) {
  puts("ZIL Header");
  printf("zh_claim_txg:     %lld\n", zh->zh_claim_txg);
  printf("zh_replay_seq:    %lld\n", zh->zh_replay_seq);
  print_block_pointer(&zh->zh_log);
  printf("zh_claim_blk_seq: %lld\n", zh->zh_claim_blk_seq);
  printf("zh_flags:         %lld\n", zh->zh_flags);
  printf("zh_claim_lr_seq:  %lld\n", zh->zh_claim_lr_seq);
  int size = blkptr_get_lsize(&zh->zh_log);
  void *buffer = malloc(size);
  read_record(pool, &zh->zh_log, buffer, size);
  hexdump_ram(buffer, 0, size);
  print_zil_chain(buffer);
  free(buffer);
}

void print_objset(pool_t *pool, const objset_phys_t *objset) {
  puts("\nOBJ SET");
  print_dnode(&objset->os_meta_dnode);
  print_zil_header(pool, &objset->os_zil_header);
}

// zio_checksum in zfs
const char *lookup_checksum(uint32_t algo) {
  switch (algo) {
  case 7:
    return "fletcher4";
  case 8:
    return "sha256";
  case 9:
    return "zilog2";
  }
  return "UNK";
}

// zio_checksum in zfs
const hash_algo_implementation *get_checksum_algo(uint8_t algo) {
  switch (algo) {
  case 8:
    return &sha256_implementation;
  default:
    return NULL;
  }
}

// zio_compress in zfs
const char *lookup_compression(uint8_t algo) {
  switch (algo) {
  case 2:
    return "uncompressed";
  case 15:
    return "lz4";
  default:
    return "UNK";
  }
}

//int LZ4_decompress_safe(const char* source, char* dest, int compressedSize, int maxDecompressedSize)
int zfs_lz4_decompress_record(void *input, int insize, void *output, int outsize) {
  uint32_t payload = BE32(*((uint32_t*)input));
  int ret = LZ4_decompress_safe(input+4, output, payload, outsize);
  //printf("decompressed to 0x%x bytes\n", ret);
  return ret;
}

void decompress_record(void *input, int insize, void *output, int outsize, int algo) {
  switch (algo) {
  case 2:
    memcpy(output, input, MIN(outsize, insize));
    break;
  case 15:
    zfs_lz4_decompress_record(input, insize, output, outsize);
    break;
  default:
    assert(0);
  }
}

void verify_checksum(void *data, int size, const hash_algo_implementation *algo, const blkptr_t *ptr) {
  if (algo == NULL) {
    puts("unable to verify unknown checksum");
    return;
  }
  uint8_t *hash = malloc(algo->hash_size);
  hash_blob(algo, data, size, hash);
  print_hash(hash, algo->hash_size);
  uint64_t *hash2 = (uint64_t*)hash;
  for (int i=0; i<4; i++) {
    assert(BE64(ptr->checksum[i]) == hash2[i]);
  }
  free(hash);
}

void read_record(pool_t *pool, const blkptr_t *ptr, void *buffer, uint32_t size) {
  if (blkptr_is_embedded(ptr)) {
    unsigned int psize = blkptr_get_embedded_psize(ptr);
    //unsigned int lsize = blkptr_get_embedded_lsize(ptr);
    uint8_t compression = blkptr_get_compression(ptr);
    //printf("reading embedded blk ptr lsize: 0x%x, psize: 0x%x, comp=%d\n", lsize, psize, compression);

    void *input_buf = malloc(112);
    void *input = input_buf;
    memcpy(input, ptr, 48);
    input += 48;

    memcpy(input, ((void*)ptr) + 56, 24);
    input += 24;

    memcpy(input, ((void*)ptr) + 88, 40);
    decompress_record(input_buf, psize, buffer, size, compression);
    free(input_buf);
  } else {
    uint32_t psize = blkptr_get_psize(ptr);
    // the offset in the dva on-disk, is in multiples of 512 bytes
    uint64_t offset = (ptr->dva[0].offset & ~(((uint64_t)1) << 63)) << 9;
    void *input = malloc(psize);
    printf("psize: 0x%x\n", psize);
    printf("offset: 0x%llx\n", offset);
    // the DVA offset is relative to 4mb into the disk
    bio_read(pool->dev, input, (4 * 1024 * 1024) + offset, psize);
    const hash_algo_implementation *checksum_algo = get_checksum_algo(blkptr_get_checksum_type(ptr));
    verify_checksum(input, psize, checksum_algo, ptr);
    uint8_t compression = blkptr_get_compression(ptr);
    decompress_record(input, psize, buffer, size, compression);
    free(input);
  }
}

void hexdump_record(pool_t *pool, const blkptr_t *ptr) {
  int size = blkptr_get_lsize(ptr);
  void *buffer = malloc(size);
  read_record(pool, ptr, buffer, size);
  hexdump_ram(buffer, 0, size);
  free(buffer);
}

void print_block_pointer(const blkptr_t *ptr) {
  if (ptr->dva[0].offset == 0) {
    puts("EMPTY");
    return;
  }
  if (blkptr_is_embedded(ptr)) {
    int lsize = blkptr_get_embedded_lsize(ptr);
    int psize = blkptr_get_embedded_psize(ptr);
    printf("embedded blk ptr size=%dL/%dP\n", lsize, psize);
  } else {
    //printf("flags: %llx\n", ptr->flags);
    assert(ptr->flags & ((uint64_t)1<<63)); // the LE flag
    for (int i=0; i<3; i++) {
      print_dva(i, ptr->dva[i].part1, ptr->dva[i].offset);
      printf(" ");
    }
    uint8_t type = (ptr->flags >> 48) & 0xff;
    uint8_t level = blkptr_get_level(ptr);
    printf("[L%d %s(%d)] ", level, lookup_object_type(type), type);
    uint32_t checksum_algo = blkptr_get_checksum_type(ptr);
    printf("%s(%d) ", lookup_checksum(checksum_algo), checksum_algo);
    uint8_t compression = blkptr_get_compression(ptr);
    printf("comp=%s(%d) ", lookup_compression(compression), compression);
    uint32_t lsize = blkptr_get_lsize(ptr);
    uint32_t psize = blkptr_get_psize(ptr);
    printf("size=0x%xL/0x%xP ", lsize, psize);
    printf("birth=%lld ", ptr->birth_txg);
    printf("cksum=%llx:%llx:%llx:%llx", ptr->checksum[0], ptr->checksum[1], ptr->checksum[2], ptr->checksum[3]);
    puts("");
  }
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

int compute_indirection_index(int blocks_per_indirect, int level, int blocknr) {
  int blocks_under_level = pow(blocks_per_indirect, level);
  int raw_index = blocknr / blocks_under_level;
  return raw_index % blocks_per_indirect;
}

// reads blocknr from a given dnode
void dnode_load_block(pool_t *pool, const dnode_phys_t *dn, unsigned int blocknr, void *buffer, int size) {
  unsigned int indirect_block_size = 1 << dn->dn_indblkshift;
  unsigned int indirect_pointers_per_block = indirect_block_size / 128;
  puts("");
  //print_dnode(dn);
  blkptr_t ptr = dn->dn_blkptr[0];
  for (int idx = dn->dn_nlevels-1; idx > 0; idx--) {
    unsigned int Ln_index = compute_indirection_index(indirect_pointers_per_block, idx - 1, blocknr);
    printf("L%d index %d\n", idx, Ln_index);
    void *iblk = malloc(indirect_block_size);
    read_record(pool, &ptr, iblk, indirect_block_size);

    puts("indirect block:");
    hexdump_ram(iblk, 0, 128 * 3);
    ptr = *(blkptr_t*)(iblk + (Ln_index * 128));

    printf("L%d is ", idx-1);
    print_block_pointer(&ptr);
    free(iblk);
    assert(idx <= 1); // L2 blocks untested
  }
  //print_block_pointer(&ptr);
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

  int size = dnode_get_data_block_size(dnode_index);
  // an array of dnode_phys_t[dnodes_per_block]
  void *block = malloc(size);
  dnode_load_block(pool, dnode_index, blocknr, block, size);
  if (0) {
    puts("chunk of dnodes");
    hexdump_ram(block, 0, size);
  }
  *dnode_out = *(dnode_phys_t*)(block + (dnode_size * index));
  free(block);
}

void hexdump_uberblock_dnode(pool_t *pool, uberblock_t *uber) {
  int lsize = blkptr_get_lsize(&uber->ub_rootbp);
  void *buffer = malloc(lsize);
  read_record(pool, &uber->ub_rootbp, buffer, lsize);
  puts("raw dnode at root block ptr");
  hexdump_ram(buffer, 0, lsize);
  free(buffer);
}

void hexdump_object_block(pool_t *pool, const dnode_phys_t *dnode_index, uint64_t objectid, int blocknr) {
  dnode_phys_t obj;
  printf("\nloading object %lld to hexdump\n", objectid);
  load_dnode_by_object_id(pool, dnode_index, objectid, &obj);
  printf("dumping dnode for object %lld\n", objectid);
  print_dnode(&obj);
  hexdump_ram(&obj, 0, 512);

  //int lsize = blkptr_get_lsize(&obj.dn_blkptr[0]);
  //void *buffer = malloc(lsize);
  //read_record(pool, &obj.dn_blkptr[0], buffer, lsize);
  //hexdump_ram(buffer, 0, lsize);
  //free(buffer);
  printf("dumping block %d of object %lld\n", blocknr, objectid);
  int data_block_size = dnode_get_data_block_size(&obj);
  void *buffer = malloc(data_block_size);
  dnode_load_block(pool, &obj, blocknr, buffer, data_block_size);
  hexdump_ram(buffer, data_block_size * blocknr, data_block_size);
  free(buffer);
}

typedef struct {
  uint64_t dir_obj;
  uint64_t prev_snap_obj;

  uint64_t prev_snap_txg;
  uint64_t next_snap_obj;

  uint64_t snapnames_zapobj;
  uint64_t num_children;

  uint64_t creation_time;
  uint64_t creation_txg;

  uint64_t deadlist_obj;
  uint64_t used_bytes;

  uint64_t compressed_bytes;
  uint64_t uncompressed_bytes;

  uint64_t unique;
  uint64_t fsid_guid;

  uint64_t guid;
  uint64_t flags;

  blkptr_t bp;
  uint64_t next_clones_obj;
  uint64_t props_obj;
  uint64_t userrefs_obj;
} dsl_dataset_phys_t;

void dump_bonus_dsl_dataset(dsl_dataset_phys_t *d) {
  printf("dir_obj:          %lld\n", d->dir_obj);
  printf("prev_snap_obj:    %lld\n", d->prev_snap_obj);

  printf("prev_snap_txg:    %lld\n", d->prev_snap_txg);
  printf("next_snap_obj:    %lld\n", d->next_snap_obj);

  printf("snapnames_zapobj: %lld\n", d->snapnames_zapobj);
  printf("num_children:     %lld\n", d->num_children);

  printf("userrefs_obj:     %lld\n", d->userrefs_obj);
  printf("creation_time:    %lld\n", d->creation_time);
  printf("creation_txg:     %lld\n", d->creation_txg);

  printf("deadlist_obj:     %lld\n", d->deadlist_obj);
  printf("used_bytes:       %lld\n", d->used_bytes);

  printf("compressed_bytes: %lld\n", d->compressed_bytes);
  printf("uncompressed_bytes: %lld\n", d->uncompressed_bytes);

  printf("unique:           %lld\n", d->unique);
  printf("fsid_guid:        0x%llx\n", d->fsid_guid);

  printf("guid:             0x%llx\n", d->guid);
  printf("flags:            %lld\n", d->flags);

  print_block_pointer(&d->bp);

  printf("next_clones_obj:  %lld\n", d->next_clones_obj);
  printf("props_obj:        %lld\n", d->props_obj);
}

void object_get_bonus(pool_t *pool, const dnode_phys_t *dnode_index, uint64_t objectid, void *out, int *size, int *type) {
  dnode_phys_t obj;
  //printf("\nloading object %lld to hexdump bonus\n", objectid);
  load_dnode_by_object_id(pool, dnode_index, objectid, &obj);
  //printf("dumping dnode for object %lld\n", objectid);
  //print_dnode(&obj);
  //hexdump_ram(&obj, 0, 512);

  void *bonus = &obj.dn_blkptr[obj.dn_nblkptr];
  memcpy(out, bonus, MIN(obj.dn_bonuslen, *size));
  *size = obj.dn_bonuslen;
  *type = obj.dn_bonustype;
}

void hexdump_object_bonus(pool_t *pool, const dnode_phys_t *dnode_index, uint64_t objectid) {
  int size, type;
  void *bonus = malloc(512);
  size = 512;
  object_get_bonus(pool, dnode_index, objectid, bonus, &size, &type);
  printf("bonus(%d):\n", type);
  hexdump_ram(bonus, 0, size);
  switch (type) {
  case 16:
    dump_bonus_dsl_dataset(bonus);
    break;
  }
  free(bonus);
}

/*
 * to import a pool:
 * import1: find the most recent uberblock
 * import2: load the dnode within the uberblocks root block pointer, that dnode is the MOS/root object set
 *
 * to access a child dataset, say test/a/b
 * object 1 in the MOS is a zap with root_dataset=32, matching step 4
 * object 32 in the MOS has a bonus with child_dir_zapobj=34
 * object 34 in the MOS is a zap with a = 257
 * object 257 in the MOS has a bonus with child_dir_zapobj=259
 * object 259 in the MOS is a zap with b=68
 * object 68 in the MOS has a bonus with head_dataset_obj, matching step 5
 * object 71 in the MOS has a bonus matching step 6, dir_obj also points backwards to obj 68
 * within dataset test/a/b, object 1 is a microzap, matching step 7
 *
 * to mount the root dataset:
 * root1: load object 1 from the MOS, it is a fat zap
 * root2: root_dataset on object 1 points to an object with a DSL directory in its bonus buffer, load that
 * root3: head_dataset_obj is a field in the bonus, it points to an object of type zap+bonus DSL dataset, load that
 * root4: in the bonus for that object is a block pointer, to the dataset object set dnode
 *
 * to mount a child dataset:
 * child1: load object 1 from the MOS, it is a fat zap
 * child2: root_dataset on object 1 points to an object with a DSL directory in its bonus buffer, load that
 * child3: child_dir_zapobj is a field in the bonus, it points to an object of type DSL directory child map
 * child4: the `DSL directory child map` is a ZAP containing childname=object#, pointing to a DSL directory
 * child5: goto step root3 or child3
 * */

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

  // step import1, find the most recent uberblock
  uint64_t latest_txg = 0;
  int latest_ub = 0;
  for (int i=0; i<128; i++) {
    uberblock_t uber;
    load_uberblock(pool, i, &uber);
    if (uber.ub_magic == 0xbab10c) {
      printf("Uberblock[%d]\n", i);
      uber_print(&uber);
      //hexdump_uberblock_dnode(pool, &uber);
      if (uber.ub_txg > latest_txg) {
        latest_txg = uber.ub_txg;
        latest_ub = i;
      }
    }
  }
  // step import2, load the dnode for the MOS
  printf("using txg %lld and uberblock %d\n", latest_txg, latest_ub);
  printf("dnode size: %ld\n", sizeof(dnode_phys_t));
  uberblock_t uber;
  load_uberblock(pool, latest_ub, &uber);
  read_record(pool, &uber.ub_rootbp, &pool->mos_dnode, sizeof(pool->mos_dnode));
  puts("\nmos dnode");
  hexdump_ram(&pool->mos_dnode, 0, 0xc0);
  print_dnode(&pool->mos_dnode);

  //dnode_phys_t object1;
  //load_dnode_by_object_id(pool, &pool->mos_dnode, 1, &object1);
  //print_dnode(&object1);
  //print_block_pointer(&object1.dn_blkptr[1]);

  //hexdump_object_block(pool, &pool->mos_dnode, 34, 0);
  //hexdump_object_bonus(pool, &pool->mos_dnode, 54);

  {
    // TODO, read head_dataset_obj from step root2, to get the 54 below
    // step root3, load the bonus for the root dataset
    puts("dumping blkptr for object 54 bonus");
    int size, type;
    dsl_dataset_phys_t *bonus = malloc(512);
    size = 512;
    object_get_bonus(pool, &pool->mos_dnode, 54, bonus, &size, &type);
    print_block_pointer(&bonus->bp);
    int lsize = blkptr_get_lsize(&bonus->bp);
    void *dnode = malloc(lsize);
    // step root4, load the dnode for the root dataset
    read_record(pool, &bonus->bp, dnode, lsize);
    print_objset(pool, dnode);
    //hexdump_ram(dnode, 0, lsize);
    free(dnode);
    free(bonus);
  }

  printf("sizeof(dnode_phys_t) == %ld\n", sizeof(dnode_phys_t));
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
  platform_halt(HALT_ACTION_SHUTDOWN, HALT_REASON_UNKNOWN);
}

APP_START(zfs_test)
  .entry = zfs_entry
APP_END

