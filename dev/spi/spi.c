#include <dev/gpio.h>
#include <endian.h>
#include <stdio.h>
#include <dev/spi.h>
#include <stdlib.h>
#include <lib/heap.h>
#include <lib/hexdump.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <platform/bcm28xx/gpio.h>

#define SPI0_BASE 0x7e204000

#define SPI_CS   (SPI0_BASE + 0x00)
#define SPI_FIFO (SPI0_BASE + 0x04)
#define SPI_CLK  (SPI0_BASE + 0x08)
#define SPI_DLEN (SPI0_BASE + 0x0c)
#define SPI_LTOH (SPI0_BASE + 0x10)

void spi_init() {
  // TODO, compute the right divisor
  *REG32(SPI_CLK) = 64;
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

static void spi_test(uint level) {
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
  uint8_t *buffer = memalign(16,8);
  uint32_t *buffer32 = (uint32_t*)buffer;
  uint32_t offset = 0;
  while (1) {
    spi_flash_read_data(buffer, offset, 8);
    uint32_t magic = ntohl(buffer32[0]);
    uint32_t length = ntohl(buffer32[1]);
    printf("found file with magic 0x%x, length %d bytes at offset 0x%x\n", magic, length, offset);

    if (magic == 0xaa55f00f) {
      stage2_offset = offset + 8;
      stage2_length = length;
    }
    offset = ROUNDUP(offset + 8 + length, 8);
    if (magic == 0xffffffff) break;
    if (magic == 0) break;
  }
  free(buffer);
}

LK_INIT_HOOK(spi, spi_test, LK_INIT_LEVEL_PLATFORM + 1);
