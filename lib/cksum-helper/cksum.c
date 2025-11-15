#include <app.h>
#include <assert.h>
#include <cksum-helper/cksum-helper.h>
#include <lib/cbuf.h>
#include <platform.h>
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

#ifdef WITH_LIB_FS
typedef struct {
  filehandle *fh;
  int offset;
  cbuf_t buf;
} file_buffer;
#endif

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
  printf("WARNING: cant find algo %s\n", algo_name);
  return 0;
}

void print_hash(const uint8_t *hash, int hash_size) {
  for (int i=0; i<hash_size; i++) {
    printf("%02x", hash[i]);
  }
  puts("");
}

void print_hash_to_string(const uint8_t *hash, int hash_size, char *outbuf) {
  int i;
  for (i=0; i<hash_size; i++) {
    sprintf(outbuf + (2*i), "%02x", hash[i]);
  }
  outbuf[2*i] = 0;
}

void hash_blob(const hash_algo_implementation *algo, void *data, int size, uint8_t *hash) {
  printf("hashing %p size 0x%x\n", data, size);
  void *context = malloc(algo->context_size);
  algo->init(context);
  algo->update(context, data, size);
  memcpy(hash, algo->finalize(context), algo->hash_size);
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
  free(hash_str);
}

#ifdef WITH_LIB_FS
status_t hash_file(const char *path, const hash_algo_implementation *algo, uint8_t *hash) {
  filehandle *fh;
  status_t ret;

  ret = fs_open_file(path, &fh);
  if (ret != 0) {
    puts("cant open");
    return ret;
  }
  void *context = malloc(algo->context_size);
  algo->init(context);
  void *buffer = malloc(1024);
  int offset = 0;
  while ((ret = fs_read_file(fh, buffer, offset, 1024)) > 0) {
    //printf("read %d bytes\n", ret);
    offset += ret;
    algo->update(context, buffer, ret);
  }
  memcpy(hash, algo->finalize(context), algo->hash_size);
  free(context);
  free(buffer);

  return 0;
}

void filebuf_init(file_buffer *buf, filehandle *fh) {
  buf->fh = fh;
  buf->offset = 0;
  cbuf_initialize(&buf->buf, 2048);
}

void paths_join(char *dest, const char *a, const char *b) {
  dest[0] = 0;
  strlcat(dest, a, FS_MAX_PATH_LEN);
  strlcat(dest, "/", FS_MAX_PATH_LEN);
  strlcat(dest, b, FS_MAX_PATH_LEN);
}

/* *
 * reads until a \n is found, or maxlen is hit
 * if a \n is found, it remains in the output buffer, and is followed by a \0
 * if a \n is NOT found, and either EOF or maxlen is reached, the buffer is not null terminated
 *
 * returns the chars read
 * */
int filebuf_readline(file_buffer *buf, char *buffer, int maxlen) {
  int ret;
  int pos = 0;
  bool eof = false;
  while (!eof) {
    if (cbuf_space_used(&buf->buf) == 0) {
      uint8_t *buffer2 = malloc(1024);
      ret = fs_read_file(buf->fh, buffer2, buf->offset, 1024);
      assert(ret >= 0); // TODO, error handling
      if (ret == 0) {
        eof = true;
      } else {
        ret = cbuf_write(&buf->buf, buffer2, ret, false);
        buf->offset += ret;
      }
      free(buffer2);
    }
    while (cbuf_space_used(&buf->buf) > 0) {
      ret = cbuf_read_char(&buf->buf, &buffer[pos], false);
      if (buffer[pos] == '\n') {
        pos++;
        buffer[pos] = '\0';
        return pos+1;
      } else if (pos == maxlen) {
        return pos+1;
      } else {
        pos++;
      }
    }
  }
  return pos;
}

bool verify_hashes(const hash_algo_implementation *algo, const char *prefix, const char *sums_file, int *matches, int *mismatches, int *failure) {
  filehandle *fh;
  status_t ret;
  if (!algo) {
    printf("algo not supplied\n");
    return true;
  }
  char *pathbuf = malloc(FS_MAX_PATH_LEN);
  void *actual_hash = malloc(algo->hash_size);
  char *actual_hash_str = malloc((algo->hash_size*2)+1);

  paths_join(pathbuf, prefix, sums_file);

  ret = fs_open_file(pathbuf, &fh);
  assert(ret == 0);

  file_buffer buf;
  filebuf_init(&buf, fh);
  const int linebuf_size = 64+2+FS_MAX_PATH_LEN+2;
  char *linebuf = malloc(linebuf_size);
  while ((ret = filebuf_readline(&buf, linebuf, linebuf_size-1)) > 0) {
    linebuf[ret] = 0;
    char *hash = strtok(linebuf, " ");
    char *name = strtok(NULL, "\n");
    while (name[0] == ' ') name++;
    //printf("hash: %s, name: '%s'\n", hash, name);
    paths_join(pathbuf, prefix, name);
    //printf("hash: %s, name: %s\n", hash, pathbuf);
    ret = hash_file(pathbuf, algo, actual_hash);
    if (ret != 0) {
      printf("%s: failure to open/read\n", name);
      (*failure)++;
      continue;
    }
    print_hash_to_string(actual_hash, algo->hash_size, actual_hash_str);
    //printf("actual hash: %s\n", actual_hash_str);

    if (strcmp(hash, actual_hash_str) == 0) {
      //printf("%s: OK\n", name);
      (*matches)++;
    } else {
      printf("%s: FAILED\n", name);
      (*mismatches)++;
    }
  }

  free(pathbuf);
  free(actual_hash_str);
  free(actual_hash);
  free(linebuf);
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

#ifdef WITH_LIB_FS
static void helper_entry(const struct app_descriptor *app, void *args) {
  test_hash_algo("sha256", "", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  status_t ret;

  ret = fs_mount("/test", "ext2", "virtio0");
  if (ret) {
    printf("mount failure: %d\n", ret);
    return;
  }
  if (1) {
    const hash_algo_implementation *algo = get_implementation("sha256");
    int matches=0, mismatches=0, failure=0;
    verify_hashes(algo, "/test/", "everything.sums", &matches, &mismatches, &failure);
    printf("%d matches, %d mismatches, %d failure\n", matches, mismatches, failure);
  }
  if (0) {
    const hash_algo_implementation *algo = get_implementation("sha256");
    void *hash = malloc(algo->hash_size);
    ret = hash_file("/test/./lk/external/platform/pico/rp2_common/pico_fix/rp2040_usb_device_enumeration/include/pico/fix/rp2040_usb_device_enumeration.h", algo, hash);
    //assert(ret == 0);
  }
  dump_thread(get_current_thread());
  platform_halt(HALT_ACTION_SHUTDOWN, HALT_REASON_UNKNOWN);
}

APP_START(cksum_helper_test)
  .entry = helper_entry,
  .flags = APP_FLAG_CUSTOM_STACK_SIZE,
  .stack_size = 64 * 1024
APP_END
#endif
