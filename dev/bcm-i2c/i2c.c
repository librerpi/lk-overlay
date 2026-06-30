#include <lib/heap.h>
#include <dev/bcm-i2c.h>
#include <lib/edid.h>
#include <lib/hexdump.h>
#include <lib/video_timing.h>
#include <lk/debug.h>
#include <lk/init.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/udelay.h>
#include <string.h>

#define CSI "\x1b["
#define RED     CSI"31m"
#define GREEN   CSI"32m"
#define DEFAULT CSI"39m"

#define LOCAL_TRACE 1

typedef struct {
  volatile uint32_t control;
  volatile uint32_t status;
  volatile uint32_t data_length;
  volatile uint32_t slave_address;
  volatile uint32_t data_fifo;
  volatile uint32_t clock_divisor;
  volatile uint32_t data_delay;
  volatile uint32_t clock_stretch_timeout;
} i2cController;

#define I2C_CONTROL_ENABLE  BV(15)
#define I2C_CONTROL_START   BV(7)
#define I2C_CONTROL_CLEAR   BV(4)
#define I2C_CONTROL_READ    BV(0)

#define I2C_STATUS_ACK_ERR  BV(8)
// RX FIFO has at least 1 byte
#define I2C_STATUS_RXD      BV(5)
// TX FIFO can accept data
#define I2C_STATUS_TXD      BV(4)
#define I2C_STATUS_DONE     BV(1)

i2cController *getController(int nr) {
  switch (nr) {
  case 0:
    return (i2cController*)BSC0_BASE;
  case 1:
    return (i2cController*)BSC1_BASE;
  // I2C2 seems to be in the HDMI power domain, and doesnt work until you bring that up
  case 2:
    return (i2cController*)BSC2_BASE;
  }
  panic("invalid i2c controller");
}

static void init_i2c_controller(int nr) {
}

static bool i2c_probe(int nr, int addr) {
  i2cController *c = getController(nr);
  c->control = I2C_CONTROL_ENABLE;
  c->clock_divisor = 2500;

  c->data_length = 0;
  c->slave_address = addr;

  c->control |= I2C_CONTROL_CLEAR | I2C_CONTROL_START;

  udelay(1000);
  //printf("0x%02x control: 0x%x ", addr, c->control);
  //printf("status: 0x%x ", c->status);
  bool ret = true;
  if (c->status & I2C_STATUS_ACK_ERR) {
    //printf("ack error");
    ret = false;
    c->status |= I2C_STATUS_ACK_ERR;
  }
  //printf("\n");
  return ret;
}

static void i2c_detect(int nr, int start, int end) {
  printf("controller: i2c%d\n", nr);
  printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");
  for (int i=start; i<end; i++) {
    if ((i % 16) == 0) printf("%02x:", i);
    if (i < 3) printf("   ");
    else {
      bool present = i2c_probe(nr, i);
      if (present) printf(" %02x", i);
      else printf(" --");
    }
    if (((i % 16) == 15) || (i == 0x77)) printf("\n");
  }
}

static int i2c_write(int nr, int addr, const uint8_t *buf, int len) {
  i2cController *c = getController(nr);
  c->control = I2C_CONTROL_ENABLE;
  c->clock_divisor = 2500;

  c->data_length = len;
  c->slave_address = addr;
  c->status |= I2C_STATUS_ACK_ERR;

  c->control |= I2C_CONTROL_CLEAR | I2C_CONTROL_START;
  for (int i=0; i<len; i++) {
    while (true) {
      uint32_t status = c->status;
      if (status & I2C_STATUS_ACK_ERR) {
        printf("ack error after %d bytes\n", i);
        return -1;
      }
      if (status & I2C_STATUS_TXD) break; // TX FIFO has room
    }
    c->data_fifo = buf[i];

  }
  while ((c->status & I2C_STATUS_DONE) == 0) {}
  if (c->status & I2C_STATUS_ACK_ERR) {
    puts("ack error 2");
    return -1;
  }
  printf("status: 0x%x\n", c->status);
  return 0;
}

static int i2c_read(int nr, int addr, uint8_t *buf, int len) {
  i2cController *c = getController(nr);
  c->control = I2C_CONTROL_ENABLE;
  c->clock_divisor = 2500;

  c->data_length = len;
  c->slave_address = addr;
  c->status |= I2C_STATUS_ACK_ERR;

  c->control |= I2C_CONTROL_CLEAR | I2C_CONTROL_START | I2C_CONTROL_READ;
  for (int i=0; i<len; i++) {
    while ((c->status & I2C_STATUS_RXD) == 0) {}
    buf[i] = c->data_fifo;

    if (c->status & I2C_STATUS_ACK_ERR) {
      printf("ack error 2 after %d bytes\n", i);
      return -1;
    }
  }
  return 0;
}

int smbus_read(int nr, int addr, int reg, uint8_t *buf, int len) {
#if LOCAL_TRACE
  printf("i2c%d R(0x%x.%d) ", nr, addr, reg);
#endif
  uint8_t addr_buf[1];
  addr_buf[0] = reg;
  int ret = i2c_write(nr, addr, addr_buf, 1);
  if (ret != 0) {
    printf("smbus_read, addr write failed\n");
    return ret;
  }
  ret = i2c_read(nr, addr, buf, len);
#if LOCAL_TRACE
  for (int i=0; i<len; i++) {
    printf("%02x ", buf[i]);
  }
  puts("");
#endif
  return 0;
}

int smbus_write(int nr, int addr, int reg, const uint8_t *buf, int len) {
#if LOCAL_TRACE
  printf("i2c%d W(0x%x.%d) ", nr, addr, reg);
  for (int i=0; i<len; i++) {
    printf("%02x ", buf[i]);
  }
  puts("");
#endif
  uint8_t *buf2 = malloc(len+1);
  buf2[0] = reg;
  memcpy(buf2 + 1, buf, len);
  int ret = i2c_write(nr, addr, buf2, len+1);
  if (ret != 0) {
    goto end;
  }
end:
  free(buf2);
  return ret;
}

#if 0
static void i2c_init(uint level) {
  printf(GREEN"probing i2c\n"DEFAULT);
  i2c_detect(2, 0, 0x78);
}

LK_INIT_HOOK(i2c, i2c_init, LK_INIT_LEVEL_PLATFORM + 20);
#endif
