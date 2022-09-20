#pragma once

typedef struct {
  int context_size;
  int hash_size;
  void (*init)(void *context);
  void (*update)(void *context, const void *data, int length);
  const uint8_t *(*finalize)(void *context);
} hash_algo_implementation;

void hash_blob(const hash_algo_implementation *algo, void *data, int size, uint8_t *hash);
void print_hash(const uint8_t *hash, int hash_size);

#ifdef WITH_LIB_MINCRYPT
extern hash_algo_implementation sha256_implementation;
#endif
