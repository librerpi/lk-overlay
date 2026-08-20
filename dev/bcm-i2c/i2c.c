#include <dev/bcm-i2c.h>
#include <dev/gpio.h>
#include <lib/edid.h>
#include <lib/heap.h>
#include <lib/hexdump.h>
#include <lib/video_timing.h>
#include <lk/console_cmd.h>
#include <lk/debug.h>
#include <lk/init.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/udelay.h>
#include <string.h>

#define CSI "\x1b["
#define RED     CSI"31m"
#define GREEN   CSI"32m"
#define DEFAULT CSI"39m"

#define LOCAL_TRACE 0

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

static int cmd_show_mux(int argc, const console_cmd_args *argv);
static int cmd_mux_set(int argc, const console_cmd_args *argv);
static int cmd_i2c_detect(int argc, const console_cmd_args *argv);
static int cmd_smbus_read(int argc, const console_cmd_args *argv);
static void i2c_detect(int nr, int start, int end);

STATIC_COMMAND_START
STATIC_COMMAND("i2c_showmux", "show i2c pinmux", &cmd_show_mux)
STATIC_COMMAND("i2c_mux", "mux i2c to a given set of pins", &cmd_mux_set)
STATIC_COMMAND("i2cdetect", "do an i2cdetect pass", &cmd_i2c_detect)
STATIC_COMMAND("smbus_read", "do an smbus read", &cmd_smbus_read)
STATIC_COMMAND_END(i2c);

static int cmd_show_mux(int argc, const console_cmd_args *argv) {
  for (int i=0; i<54; i++) {
    int config = gpio_get_config(i);
    switch (i) {
    case 0:
    case 28:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SDA0");
      break;
    case 1:
    case 29:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SCL0");
      break;
    case 2:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SDA1");
      break;
    case 3:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SCL1");
      break;
    case 44:
      if (config == kBCM2708Pinmux_ALT1) printf("GPIO%02d: %s\n", i, "SDA0");
      if (config == kBCM2708Pinmux_ALT2) printf("GPIO%02d: %s\n", i, "SDA1");
      break;
    case 45:
      if (config == kBCM2708Pinmux_ALT1) printf("GPIO%02d: %s\n", i, "SCL0");
      if (config == kBCM2708Pinmux_ALT2) printf("GPIO%02d: %s\n", i, "SCL1");
      break;
    case 46:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SDA0");
      if (config == kBCM2708Pinmux_ALT1) printf("GPIO%02d: %s\n", i, "SDA1");
      break;
    case 47:
      if (config == kBCM2708Pinmux_ALT0) printf("GPIO%02d: %s\n", i, "SCL0");
      if (config == kBCM2708Pinmux_ALT1) printf("GPIO%02d: %s\n", i, "SCL1");
      break;
    }
  }
  return 0;
}

static int cmd_mux_set(int argc, const console_cmd_args *argv) {
  if (argc != 3) {
    printf("usage: i2c_mux <controller_nr> <basepin>\n");
    return 0;
  }
  int controller = argv[1].u;
  int basepin = argv[2].u;
  if ((controller == 0) && (basepin == 0)) {
    gpio_config(0, kBCM2708Pinmux_ALT0);
    gpio_config(1, kBCM2708Pinmux_ALT0);
  } else if ((controller == 1) && (basepin == 2)) {
    gpio_config(2, kBCM2708Pinmux_ALT0);
    gpio_config(3, kBCM2708Pinmux_ALT0);
  } else if ((controller == 0) && (basepin == 28)) {
    gpio_config(28, kBCM2708Pinmux_ALT0);
    gpio_config(29, kBCM2708Pinmux_ALT0);
  } else if ((controller == 0) && (basepin == 44)) {
    gpio_config(44, kBCM2708Pinmux_ALT1);
    gpio_config(45, kBCM2708Pinmux_ALT1);
  } else if ((controller == 1) && (basepin == 44)) {
    gpio_config(44, kBCM2708Pinmux_ALT2);
    gpio_config(45, kBCM2708Pinmux_ALT2);
  } else if ((controller == 0) && (basepin == 46)) {
    gpio_config(46, kBCM2708Pinmux_ALT0);
    gpio_config(47, kBCM2708Pinmux_ALT0);
  } else if ((controller == 1) && (basepin == 46)) {
    gpio_config(46, kBCM2708Pinmux_ALT1);
    gpio_config(47, kBCM2708Pinmux_ALT1);
  } else {
    printf("invalid pin chosen\n");
  }
  return 0;
}

static int cmd_i2c_detect(int argc, const console_cmd_args *argv) {
  int controller = 0;
  int start = 0;
  int end = 0x78;
  if (argc >= 2) controller = argv[1].u;
  if (argc >= 3) start = argv[2].u;
  if (argc >= 4) end = argv[3].u;
  i2c_detect(controller, start, end);
  return 0;
}

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

static int cmd_smbus_read(int argc, const console_cmd_args *argv) {
  int controller = argv[1].u;
  int addr = argv[2].u;
  int reg = argv[3].u;
  int size = argv[4].u;
  uint8_t *buffer = malloc(size);
  smbus_read(controller, addr, reg, buffer, size);
  for (int i=0; i<size; i++) {
    printf("%02x ", buffer[i]);
    if ((i % 16) == 15) puts("");
  }
  puts("");
  free(buffer);
  return 0;
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
