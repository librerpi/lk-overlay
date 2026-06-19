#include <dev/gpio.h>
#include <platform/bcm28xx/clock.h>
#include <cksum-helper/cksum-helper.h>
#include <dev/spi.h>
#include <endian.h>
#include <lib/heap.h>
#include <lib/hexdump.h>
#include <lk/init.h>
#include <lk/list.h>
#include <lk/reg.h>
#include <platform/bcm28xx/gpio.h>
#include <stdio.h>
#include <stdlib.h>

#define SPI_CS   (SPI0_BASE + 0x00)
#define SPI_FIFO (SPI0_BASE + 0x04)
#define SPI_CLK  (SPI0_BASE + 0x08)
#define SPI_DLEN (SPI0_BASE + 0x0c)
#define SPI_LTOH (SPI0_BASE + 0x10)

void spi_init() {
  // TODO, compute the right divisor
  *REG32(SPI_CLK) = 16;
  *REG32(SPI_CS) = 0;
  gpio_config(8, kBCM2708Pinmux_ALT0);
  gpio_config(9, kBCM2708Pinmux_ALT0);
  gpio_config(10, kBCM2708Pinmux_ALT0);
  gpio_config(11, kBCM2708Pinmux_ALT0);
}

void spi_begin() {
  *REG32(SPI_CS) = 1 << 7;
}

static void spi_out_transfer(uint8_t *output, size_t length) {
  while (length) {
    while ( (*REG32(SPI_CS) & (1<<18)) == 0) {}
    *REG32(SPI_FIFO) = *output;
    output++;
    length--;
  }
}

static void spi_in_transfer(uint8_t *input, size_t length) {
  while (length) {
    while ( (*REG32(SPI_CS) & (1<<17)) == 0) {}
    *input = *REG32(SPI_FIFO);
    *REG32(SPI_FIFO) = 0;
    input++;
    length--;
  }
}

void spi_end() {
  *REG32(SPI_CS) = 0;
}

void spi_put_byte(uint8_t byte) {
  while ( (*REG32(SPI_CS) & (1<<18)) == 0) {}
  *REG32(SPI_FIFO) = byte;
}

void spi_flash_read_data(uint8_t *buffer, uint32_t offset, size_t length) {
  spi_begin();
  spi_put_byte(0x03); // read data command
  spi_put_byte((offset >> 16) & 0xff);
  spi_put_byte((offset >> 8) & 0xff);
  spi_put_byte(offset & 0xff);

  for (int i=0; i<4; i++) {
    while ( (*REG32(SPI_CS) & (1<<17)) == 0) {}
    uint8_t t = *REG32(SPI_FIFO);
    (void)t;
  }

  for (uint i=0; i<length; i++) {
    spi_put_byte(0);
    while ( (*REG32(SPI_CS) & (1<<17)) == 0) {}
    buffer[i] = *REG32(SPI_FIFO);
  }
  spi_end();
}

uint32_t stage2_offset;
uint32_t stage2_length;

typedef struct {
  uint32_t magic;
  uint32_t length;
  char filename[16];
} spi_file_header;

typedef struct {
  struct list_node node;
  uint32_t offset;
  uint32_t length; // includes the 32byte sha256 at the end
  char filename[16];
} spi_file_cache;

static struct list_node discoveredFiles;

ssize_t spi_read_file(const char *filename, uint8_t **buffer) {
  spi_file_cache *fc;
  list_for_every_entry(&discoveredFiles, fc, spi_file_cache, node) {
    printf("%s vs %s\n", filename, fc->filename);
    if (strncmp(filename, fc->filename, 16) == 0) {
      if (buffer != NULL) {
        uint32_t start = *REG32(ST_CLO);

        *buffer = malloc(fc->length);
        spi_flash_read_data(*buffer, fc->offset, fc->length);

        uint32_t middle = *REG32(ST_CLO);

        uint8_t hash[sha256_implementation.hash_size];
        uint8_t *expected_hash = *buffer + fc->length - 32;
        hash_blob(&sha256_implementation, *buffer, fc->length - 32, hash);

        uint32_t end = *REG32(ST_CLO);

        if (memcmp(hash, expected_hash, 32) != 0) {
          printf("hash mismatch, got: ");
          print_hash(hash, 32);
          printf(" expected: ");
          print_hash(expected_hash, 32);
          puts("");
          free(*buffer);
          *buffer = NULL;
          return -1;
        }
        printf("loaded %d bytes from SPI flash in %d uSec, hashed in %d uSec\n", fc->length, middle - start, end - middle);
        float rate = ((float) fc->length / (middle - start)) * 1000000;
        printf("SPI rate, %d kbytes/sec\n", ((int)rate) >> 10);
        rate = ((float) fc->length / (end - middle)) * 1000000;
        printf("hash rate, %d kbytes/sec\n", ((int)rate) >> 10);
      }
      return fc->length - 32;
    }
  }
  return -1;
}

static void spi_test(uint level) {
  list_initialize(&discoveredFiles);
  spi_init();
#if 0
  spi_begin();
  uint8_t buffer[1] = { 3 };
  uint8_t reply[16];
  spi_out_transfer(buffer, 1);
  spi_in_transfer(reply, 16);
  spi_end();
  hexdump_ram(reply, 0, 16);
#endif
  uint8_t *buffer = memalign(16,8 + 16);
  spi_file_header *header = (spi_file_header*)buffer;
  uint32_t offset = 0;
  while (1) {
    spi_flash_read_data(buffer, offset, 8 + 16);
    header->magic = ntohl(header->magic);
    header->length = ntohl(header->length);
    printf("found entry with magic 0x%x, length %d bytes at offset 0x%x\n", header->magic, header->length, offset);

    if (header->magic == 0xaa55f00f) {
      stage2_offset = offset + 8;
      stage2_length = header->length;
    } else if (header->magic == 0xaa55f11f) {
      spi_file_cache *newEntry = malloc(sizeof(spi_file_cache));
      newEntry->offset = offset + 8 + 16;
      newEntry->length = header->length - 16;
      memcpy(newEntry->filename, header->filename, 16);

      list_add_tail(&discoveredFiles, &newEntry->node);
    } else if (header->magic == 0xffffffff) break;
    else if (header->magic == 0) break;

    offset = ROUNDUP(offset + 8 + header->length, 8);
  }
  free(buffer);
}

LK_INIT_HOOK(spi, spi_test, LK_INIT_LEVEL_PLATFORM + 1);
