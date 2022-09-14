#include <app.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_LIB_MINCRYPT
#include <lib/mincrypt/sha.h>
#include <lib/mincrypt/sha256.h>
#endif

#ifdef WITH_LIB_FS
#include <lib/fs.h>
#endif

#ifdef WITH_APP_SHELL
#include <lk/console_cmd.h>
#endif

typedef struct {
  int context_size;
  int hash_size;
  void (*init)(void *context);
  void (*update)(void *context, const void *data, int length);
  const uint8_t *(*finalize)(void *context);
} hash_algo_implementation;

#ifdef WITH_LIB_MINCRYPT
static void sha256_init(void *context) {
  SHA256_init(context);
}
static void sha256_update(void *context, const void *data, int length) {
  SHA256_update(context, data, length);
}
const uint8_t *sha256_finalize(void *context) {
  return SHA256_final(context);
}

hash_algo_implementation sha256_implementation = {
  .context_size = sizeof(SHA256_CTX),
  .hash_size = SHA256_DIGEST_SIZE,
  .init = &sha256_init,
  .update = &sha256_update,
  .finalize = &sha256_finalize,
};
#endif

const hash_algo_implementation *get_implementation(const char *algo_name) {
  if (0) {
  }
#ifdef WITH_LIB_MINCRYPT
  else if (strcmp(algo_name, "sha256") == 0) {
    return &sha256_implementation;
  }
#endif
  return 0;
}

void print_hash(const uint8_t *hash, int hash_size) {
  for (int i=0; i<hash_size; i++) {
    printf("%02x", hash[i]);
  }
  puts("");
}

void test_hash_algo(const char *name, const void *data, int size, const char *expected) {
  const hash_algo_implementation *algo = get_implementation(name);
  if (!algo) {
    printf("algo %s not found\n", name);
    return;
  }
  void *context = malloc(algo->context_size);
  algo->init(context);
  algo->update(context, data, size);
  const uint8_t *hash = algo->finalize(context);
  char *hash_str = malloc((algo->hash_size * 2)+1);
  for (int i=0; i<algo->hash_size; i++) {
    snprintf(hash_str + (i*2), 3, "%02x", hash[i]);
  }
  hash_str[algo->hash_size * 2] = 0;
  if (strcmp(expected, hash_str) == 0) {
    printf("%s: hashes match\n", name);
  } else {
    printf("%s hashes dont match, %s != %s\n", name, expected, hash_str);
  }
  free(context);
}

#ifdef WITH_LIB_FS
void hash_file(const char *path, const hash_algo_implementation *algo, uint8_t *hash) {
  filehandle *fh;
  status_t ret;

  ret = fs_open_file(path, &fh);
  void *context = malloc(algo->context_size);
  algo->init(context);
  void *buffer = malloc(1024);
  int offset = 0;
  while ((ret = fs_read_file(fh, buffer, offset, 1024)) > 0) {
    printf("read %d bytes\n", ret);
    offset += ret;
    algo->update(context, buffer, ret);
  }
  memcpy(hash, algo->finalize(context), algo->hash_size);
  free(context);
  free(buffer);
}

bool verify_hashes(const hash_algo_implementation *algo, const char *prefix, const char *sums_file) {
  filehandle *fh;
  status_t ret;

  ret = fs_open_file(path, &fh);
  return false;
}
#endif

#if defined(WITH_LIB_FS) && defined(WITH_APP_SHELL)
static int cmd_hash_file(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("hash_file", "hash a file", &cmd_hash_file)
STATIC_COMMAND_END(cksum_helper);

static int cmd_hash_file(int argc, const console_cmd_args *argv) {
  if (argc < 3) {
    printf("usage: %s <algo> <path>\n", argv[0].str);
    return 0;
  }
  const hash_algo_implementation *algo = get_implementation(argv[1].str);
  if (!algo) {
    printf("unsupported hash algo: %s\n", argv[1].str);
    return 0;
  }
  void *hash = malloc(algo->hash_size);
  hash_file(argv[2].str, algo, hash);
  print_hash(hash, algo->hash_size);
  free(hash);
  return 0;
}
#endif

static void helper_entry(const struct app_descriptor *app, void *args) {
  test_hash_algo("sha256", "", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  status_t ret;

  ret = fs_mount("/test", "ext2", "virtio0");
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  const hash_algo_implementation *algo = get_implementation("sha256");
  verify_hashes(algo, "/test/", "everything.sums");
}

APP_START(helper_test)
  .entry = helper_entry
APP_END

