/*=============================================================================
Copyright (C) 2016-2017 Authors of rpi-open-firmware
All rights reserved.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

FILE DESCRIPTION
SDHOST driver. This used to be known as ALTMMC.

=============================================================================*/

#include "sd_proto.hpp"
#include "block_device.hpp"

#include <endian.h>
#include <lib/bio.h>
#include <lib/partition.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/init.h>
#include <lk/reg.h>
#include <malloc.h>
#include <platform/bcm28xx.h>
#include <platform/bcm28xx/gpio.h>
#include <platform/bcm28xx/pll.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <platform/bcm28xx/sdhost.h>
#include <platform/bcm28xx/sdhost_impl.h>
#include <platform/bcm28xx/udelay.h>
#include <stdio.h>

#include "lk/trace.h"
#include "kernel/thread.h"

extern "C" {
  #include <dev/gpio.h>
  static bdev_t *sd;
}

#define SDEDM_WRITE_THRESHOLD_SHIFT 9
#define SDEDM_READ_THRESHOLD_SHIFT 14
#define SDEDM_THRESHOLD_MASK     0x1f

#define SAFE_READ_THRESHOLD     4
#define SAFE_WRITE_THRESHOLD    4

#define VOLTAGE_SUPPLY_RANGE 0x100
#define CHECK_PATTERN 0x55

#define SDHSTS_BUSY_IRPT                0x400
#define SDHSTS_BLOCK_IRPT               0x200
#define SDHSTS_SDIO_IRPT                0x100
#define SDHSTS_REW_TIME_OUT             0x80
#define SDHSTS_CMD_TIME_OUT             0x40
#define SDHSTS_CRC16_ERROR              0x20
#define SDHSTS_CRC7_ERROR               0x10
#define SDHSTS_FIFO_ERROR               0x08

#define SDEDM_FSM_MASK           0xf
#define SDEDM_FSM_IDENTMODE      0x0
#define SDEDM_FSM_DATAMODE       0x1
#define SDEDM_FSM_READDATA       0x2
#define SDEDM_FSM_WRITEDATA      0x3
#define SDEDM_FSM_READWAIT       0x4
#define SDEDM_FSM_READCRC        0x5
#define SDEDM_FSM_WRITECRC       0x6
#define SDEDM_FSM_WRITEWAIT1     0x7
#define SDEDM_FSM_POWERDOWN      0x8
#define SDEDM_FSM_POWERUP        0x9
#define SDEDM_FSM_WRITESTART1    0xa
#define SDEDM_FSM_WRITESTART2    0xb
#define SDEDM_FSM_GENPULSES      0xc
#define SDEDM_FSM_WRITEWAIT2     0xd
#define SDEDM_FSM_STARTPOWDOWN   0xf

#define SDHSTS_TRANSFER_ERROR_MASK      (SDHSTS_CRC7_ERROR|SDHSTS_CRC16_ERROR|SDHSTS_REW_TIME_OUT|SDHSTS_FIFO_ERROR)
#define SDHSTS_ERROR_MASK               (SDHSTS_CMD_TIME_OUT|SDHSTS_TRANSFER_ERROR_MASK)

#define logf(fmt, ...) { print_timestamp(); printf("[EMMC:%s]: " fmt, __FUNCTION__, ##__VA_ARGS__); }
#define mfence() __sync_synchronize()

#define LOCAL_TRACE 1

struct BCM2708SDHost : BlockDevice {
  bool is_sdhc;
  bool is_high_capacity;
  bool card_ready;

  uint32_t ocr;
  uint32_t rca;

  uint32_t cid[4];
  uint32_t csd[4];
  uint64_t scr;

  uint64_t capacity_bytes;

  uint32_t r[4];

  uint32_t current_cmd;

  void set_power(bool on) {
    *REG32(SH_VDD) = on ? SH_VDD_POWER_ON_SET : 0x0;
  }

  bool wait(uint32_t timeout = 100000) {
    uint32_t t = timeout;

    while(*REG32(SH_CMD) & SH_CMD_NEW_FLAG_SET) {
      if (t == 0) {
        logf("timed out after %dus!\n", timeout)
        return false;
      }
      t--;
      udelay(10);
    }

    return true;
  }

  // waits for previous command to finish first
  // allows sending commands with extra flags set
  bool send_raw(uint32_t command, uint32_t arg = 0) {
    uint32_t sts;

    wait();
    LTRACEF("CMD %d, arg=0x%x\n", command & SH_CMD_COMMAND_SET, arg);

    sts = *REG32(SH_HSTS);
    if (sts & SDHSTS_ERROR_MASK)
      *REG32(SH_HSTS) = sts;

    current_cmd = command & SH_CMD_COMMAND_SET;

    *REG32(SH_ARG) = arg;
    *REG32(SH_CMD) = command | SH_CMD_NEW_FLAG_SET;

    mfence();

    return true;
  }

  // sends a normal command, and filters it to 0-63
  bool send(uint32_t command, uint32_t arg = 0) {
    return send_raw(command & SH_CMD_COMMAND_SET, arg);
  }

  bool send_136_resp(uint32_t command, uint32_t arg = 0) {
    return send_raw((command & SH_CMD_COMMAND_SET) | SH_CMD_LONG_RESPONSE_SET, arg);
  }

  bool send_no_resp(uint32_t command, uint32_t arg = 0) {
    return send_raw((command & SH_CMD_COMMAND_SET) | SH_CMD_NO_RESPONSE_SET, arg);
  }

  // use before every ACMD
  bool send_cmd55() {
    send(MMC_APP_CMD, MMC_ARG_RCA(rca));
    return wait_and_get_response();
  }

  void configure_pinmux() {
    gpio_config(48, kBCM2708Pinmux_ALT0);
    gpio_config(49, kBCM2708Pinmux_ALT0);
    gpio_config(50, kBCM2708Pinmux_ALT0);
    gpio_config(51, kBCM2708Pinmux_ALT0);
    gpio_config(52, kBCM2708Pinmux_ALT0);
    gpio_config(53, kBCM2708Pinmux_ALT0);

    struct gpio_pull_batch pullBatch;
    GPIO_PULL_CLEAR(pullBatch);
    GPIO_PULL_SET(pullBatch, 48, kPullUp);
    GPIO_PULL_SET(pullBatch, 49, kPullUp);
    GPIO_PULL_SET(pullBatch, 50, kPullUp);
    GPIO_PULL_SET(pullBatch, 51, kPullUp);
    GPIO_PULL_SET(pullBatch, 52, kPullUp);
    GPIO_PULL_SET(pullBatch, 53, kPullUp);
    gpio_apply_batch(&pullBatch);

    //logf("pinmux configured for alt0\n");
  }

  bool set_bus_width(int width) {
    //for (;;) {
      send_cmd55();
      send_no_resp(SD_APP_SET_BUS_WIDTH, width);

      if (!wait_and_get_response()) return false;

      //logf("waiting for SD (0x%x) ...\n", r[0]);
      udelay(100);
    //}
    *REG32(SH_HCFG) |= SH_HCFG_WIDE_EXT_BUS_SET;
    return true;
  }

  // based on linux
  bool sd_switch(int mode, int group, int value, uint32_t *resp) {
    LTRACEF("(%d, %d, %d, %p)\n", mode, group, value, resp);
    uint32_t arg = (!!mode) << 31 | 0xffffff;
    arg &= ~(0xF << (group * 4));
    arg |= (value & 0xF) << (group * 4);

    *REG32(SH_HBCT) = 64;
    *REG32(SH_HBLC) = 1;
    send_raw(SD_SEND_SWITCH_FUNC | SH_CMD_READ_CMD_SET, arg);

    if (wait_and_get_response()) {
      return read_data(resp, 64);
    } else {
      logf("CMD6 fail\n");
      return false;
    }
  }

  bool read_data(uint32_t *resp, uint32_t size) {
    for (unsigned int i=0; i<(size/4); i++) {
      if (!wait_for_fifo_data()) {
        return false;
      }
      *(resp++) = *REG32(SH_DATA);
    }
    return true;
  }

	void reset() {
		logf("resetting controller ...\n");
		set_power(false);

		*REG32(SH_CMD) = 0;
		*REG32(SH_ARG) = 0;
		*REG32(SH_TOUT) = 0xF00000;
		*REG32(SH_CDIV) = 0;
		*REG32(SH_HSTS) = 0x7f8;
		*REG32(SH_HCFG) = 0;
		*REG32(SH_HBCT) = 0;
		*REG32(SH_HBLC) = 0;

		uint32_t temp = *REG32(SH_EDM);

		temp &= ~((SDEDM_THRESHOLD_MASK<<SDEDM_READ_THRESHOLD_SHIFT) |
		          (SDEDM_THRESHOLD_MASK<<SDEDM_WRITE_THRESHOLD_SHIFT));
		temp |= (SAFE_READ_THRESHOLD << SDEDM_READ_THRESHOLD_SHIFT) |
		        (SAFE_WRITE_THRESHOLD << SDEDM_WRITE_THRESHOLD_SHIFT);

		*REG32(SH_EDM) = temp;
		udelay(300);

		set_power(true);

		udelay(300);
		mfence();
	}

  inline void get_response() {
    r[0] = *REG32(SH_RSP0);
    r[1] = *REG32(SH_RSP1);
    r[2] = *REG32(SH_RSP2);
    r[3] = *REG32(SH_RSP3);
  }

  bool wait_and_get_response() {
    if (!wait())
      return false;

    get_response();

    LTRACEF("Cmd: %d Resp: %08x %08x %08x %08x\n", current_cmd, r[0], r[1], r[2], r[3]);

    if (*REG32(SH_CMD) & SH_CMD_FAIL_FLAG_SET) {
      if (*REG32(SH_HSTS) & SDHSTS_ERROR_MASK) {
        logf("ERROR: sdhost status: 0x%x\n", *REG32(SH_HSTS));
        return false;
      }
      logf("ERROR: unknown error, SH_CMD=0x%x\n", *REG32(SH_CMD));
      return false;
    }


    return true;
  }

  bool query_voltage_and_type() {
    uint32_t t;
    LTRACEF("\n");

    /* identify */
    send(SD_SEND_IF_COND, 0x1AA);
    if (!wait_and_get_response()) return false;

    /* set voltage */
    t = MMC_OCR_3_3V_3_4V;
    if (r[0] == 0x1AA) {
      t |= MMC_OCR_HCS;
      is_sdhc = true;
    }

    /* query voltage and type */
    for (;;) {
      send_cmd55();
      send_no_resp(SD_APP_OP_COND, t);

      if (!wait_and_get_response()) return false;

      if (r[0] & MMC_OCR_MEM_READY) break;

      //logf("waiting for SD (0x%x) ...\n", r[0]);
      udelay(100);
    }


    logf("SD card has arrived!\n");

    is_high_capacity = (r[0] & MMC_OCR_HCS) == MMC_OCR_HCS;

    if (is_high_capacity) logf("This is an SDHC card!\n");

    return true;
  }

	inline void copy_136_to(uint32_t* dest) {
		dest[0] = r[0];
		dest[1] = r[1];
		dest[2] = r[2];
		dest[3] = r[3];
	}

  bool identify_card() {
    logf("identifying card ...\n");

    send_136_resp(MMC_ALL_SEND_CID);
    if (!wait_and_get_response())
            return false;

    /* for SD this gets RCA */
    send(MMC_SET_RELATIVE_ADDR);
    if (!wait_and_get_response())
            return false;
    rca = SD_R6_RCA(r);

    //logf("RCA = 0x%x\n", rca);

    send_136_resp(MMC_SEND_CID, MMC_ARG_RCA(rca));
    if (!wait_and_get_response())
            return false;

    copy_136_to(cid);

    /* get card specific data */
    send_136_resp(MMC_SEND_CSD, MMC_ARG_RCA(rca));
    if (!wait_and_get_response())
            return false;

    copy_136_to(csd);

    scr = 0;
    logf("sending ACMD51\n");
    *REG32(SH_HBCT) = 8;
    *REG32(SH_HBLC) = 1;
    select_card();
    send_cmd55();
    send_raw(SD_APP_SEND_SCR | SH_CMD_READ_CMD_SET);
    if (wait_and_get_response()) {
      for (int i=0; i<2; i++) {
        wait_for_fifo_data();
        scr |= (uint64_t)(htonl(*REG32(SH_DATA))) << ( (1-i) * 32);
      }
      logf("done 0x%llx\n", scr);
    } else {
      logf("ACMD51 fail\n");
    }

    return true;
  }

//#define DUMP_READ

  bool wait_for_fifo_data(uint32_t timeout = 100000) {
    uint32_t t = timeout;

    while ((*REG32(SH_HSTS) & SH_HSTS_DATA_FLAG_SET) == 0) {
      if (t == 0) {
        logf("ERROR: no FIFO data, timed out after %dus!\n", timeout)
        return false;
      }
      t--;
      udelay(1);
    }

    return true;
  }

  void drain_fifo() {
    /* fuck me with a rake ... gently */

    wait();

    while (*REG32(SH_HSTS) & SH_HSTS_DATA_FLAG_SET) {
      *REG32(SH_DATA);
      mfence();
    }
  }

  void drain_fifo_nowait() {
    while (true) {
      *REG32(SH_DATA);

      uint32_t hsts = *REG32(SH_HSTS);
      if (hsts != SH_HSTS_DATA_FLAG_SET)
        break;
    }
  }

  bool real_read_block(bnum_t sector, uint32_t* buf, uint32_t count) {
    int chunks = 128 * count;

    *REG32(SH_HBCT) = block_size;
    *REG32(SH_HBLC) = count;

    if (!card_ready)
            panic("card not ready");

    if (!is_high_capacity)
            sector <<= 9;

#ifdef DUMP_READ
    if (buf) {
      logf("Reading %d bytes from sector %d using FIFO ...\n", block_size, sector);
    } else {
      logf("Reading %d bytes from sector %d using FIFO > /dev/null ...\n", block_size, sector);
    }
#endif

    /* drain junk from FIFO */
    drain_fifo();

    /* enter READ mode */
    if (count == 1) {
      send_raw(MMC_READ_BLOCK_SINGLE | SH_CMD_READ_CMD_SET | SH_CMD_BUSY_CMD_SET, sector);
    } else {
      send_raw(MMC_READ_BLOCK_MULTIPLE | SH_CMD_READ_CMD_SET, sector);
    }
    wait();

    int i;
    uint32_t hsts_err = 0;


#ifdef DUMP_READ
    if (buf)
       printf("----------------------------------------------------\n");
#endif

    /* drain useful data from FIFO */
    for (i = 0; i < chunks; i++) {
      /* wait for FIFO */
      if (!wait_for_fifo_data()) {
              break;
      }

      hsts_err = *REG32(SH_HSTS) & SDHSTS_ERROR_MASK;
      if (hsts_err) {
        logf("ERROR: transfer error on FIFO word %d: 0x%x\n", i, *REG32(SH_HSTS));
        break;
      }


      volatile uint32_t data = *REG32(SH_DATA);

#ifdef DUMP_READ
      printf("%08x ", data);
#endif
      if (buf) *(buf++) = data;
    }

    send_raw(MMC_STOP_TRANSMISSION | SH_CMD_BUSY_CMD_SET);

#ifdef DUMP_READ
    printf("\n");
    if (buf)
      printf("----------------------------------------------------\n");
#endif

    if (hsts_err) {
      logf("ERROR: Transfer error, status: 0x%x\n", *REG32(SH_HSTS));
      return false;
    }

#ifdef DUMP_READ
    if (buf)
      logf("Completed read for %d\n", sector);
#endif
    return true;
  }



	bool select_card() {
		send(MMC_SELECT_CARD, MMC_ARG_RCA(rca));

		if (!wait())
			return false;

		return true;
	}

  bool init_card() {
    char pnm[8];
    uint64_t block_length;
    uint32_t clock_div = 0;
    LTRACEF("\n");

    send_no_resp(MMC_GO_IDLE_STATE);

    if (!query_voltage_and_type()) {
      //logf("ERROR: Failed to query card voltage!\n");
      return false;
    }

    if (!identify_card()) {
      //logf("ERROR: Failed to identify card!\n");
      return false;
    }

    SD_CID_PNM_CPY(cid, pnm);

    //logf("Detected SD card:\n");
    printf("    Product : %s\n", pnm);

    if (SD_CSD_CSDVER(csd) == SD_CSD_CSDVER_2_0) {
      printf("    CSD     : Ver 2.0\n");
      printf("    Capacity: %d\n", SD_CSD_V2_CAPACITY(csd));
      printf("    Size    : %d\n", SD_CSD_V2_C_SIZE(csd));

      block_length = 1 << SD_CSD_V2_BL_LEN;

      /* work out the capacity of the card in bytes */
      capacity_bytes = (SD_CSD_V2_CAPACITY(csd) * block_length);

      clock_div = vpu_clock / 25; // 5;
    } else if (SD_CSD_CSDVER(csd) == SD_CSD_CSDVER_1_0) {
      printf("    CSD     : Ver 1.0\n");
      printf("    Capacity: %d\n", SD_CSD_CAPACITY(csd));
      printf("    Size    : %d\n", SD_CSD_C_SIZE(csd));

      block_length = 1 << SD_CSD_READ_BL_LEN(csd);

      /* work out the capacity of the card in bytes */
      capacity_bytes = (SD_CSD_CAPACITY(csd) * block_length);

      clock_div = vpu_clock / 25;
    } else {
      printf("ERROR: Unknown CSD version 0x%x!\n", SD_CSD_CSDVER(csd));
      return false;
    }

    printf("    BlockLen: 0x%llx\n", block_length);

    if (!select_card()) {
      //logf("ERROR: Failed to select card!\n");
      return false;
    }

    uint32_t switch_reply[64/4];
    sd_switch(0, 0, 1, switch_reply);
    for (unsigned int i=0; i<(64/4); i++) {
      printf("0x%08x ", htonl(switch_reply[i]));
    }
    puts("");
    sd_switch(1, 0, 1, switch_reply);
    for (unsigned int i=0; i<(64/4); i++) {
      printf("0x%08x ", htonl(switch_reply[i]));
    }
    puts("");

    if (true) {
      // set bus width to 4bit
      set_bus_width(2);
      logf("card now in 4bit mode\n");
    }

    if (SD_CSD_CSDVER(csd) == SD_CSD_CSDVER_1_0) {
      /*
       * only needed for 1.0 ones, the 2.0 ones have this
       * fixed at 512.
       */
      //logf("Setting block length to 512 ...\n");
      send(MMC_SET_BLOCKLEN, 512);
      if (!wait()) {
        logf("ERROR: Failed to set block length!\n");
        return false;
      }
    }

    block_size = 512;
    printf("vpu clock is %d, cdiv %d\n", vpu_clock, clock_div);

    logf("Card initialization complete: %s %dMB SD%s Card\n", pnm, (uint32_t)(capacity_bytes >> 20), is_high_capacity ? "HC" : "");

    /*
     * this makes some dangerous assumptions that the all csd2 cards are sdio cards
     * and all csd1 cards are sd cards and that mmc cards won't be used. this also assumes
     * PLLC.CORE0 is at vpu_clock Hz which is probably a safe assumption since we set it.
     */
    if (clock_div) {
      logf("Identification complete, changing clock to %dMHz for data mode ...\n", vpu_clock / clock_div);
      set_clock_div(clock_div);
    }

    return true;
  }

  void set_clock_div(uint32_t clock_div) {
    logf("setting clock to %dMHz\n", (uint32_t)(vpu_clock / clock_div));
    *REG32(SH_CDIV) = clock_div - 2;
  }

  status_t restart_controller() {
    LTRACEF("\n");
    is_sdhc = false;
    rca = 0;

    logf("hcfg 0x%X, cdiv 0x%X, edm 0x%X, hsts 0x%X\n",
         *REG32(SH_HCFG),
         *REG32(SH_CDIV),
         *REG32(SH_EDM),
         *REG32(SH_HSTS));

    logf("Restarting the eMMC controller ...\n");

    configure_pinmux();
    reset();

    *REG32(SH_HCFG) &= ~SH_HCFG_WIDE_EXT_BUS_SET;
    *REG32(SH_HCFG) = SH_HCFG_SLOW_CARD_SET | SH_HCFG_WIDE_INT_BUS_SET;
    *REG32(SH_CDIV) = (vpu_clock * 1000) / 125;

    udelay(300);
    mfence();

    if (init_card()) {
      card_ready = true;

      /*
       * looks like a silicon bug to me or a quirk of csd2, who knows
       */
      for (int i = 0; i < 3; i++) {
        if (!real_read_block(0, nullptr, 1)) {
          panic("fifo flush cycle %d failed\n", i);
        }
      }
      return NO_ERROR;
    } else {
      card_ready = false;
      puts("failed to reinitialize the eMMC controller");
      return ERR_NOT_FOUND;
    }
  }

	void stop() {
		if (card_ready) {
			logf("flushing fifo ...\n");
			drain_fifo_nowait();

			logf("asking card to enter idle state ...\n");
			*REG32(SH_CDIV) = (vpu_clock * 1000) / 125;
			udelay(150);

			send_no_resp(MMC_GO_IDLE_STATE);
			udelay(500);
		}

		logf("stopping sdhost controller driver ...\n");

		*REG32(SH_CMD) = 0;
		*REG32(SH_ARG) = 0;
		*REG32(SH_TOUT) = 0xA00000;
		*REG32(SH_CDIV) = 0x1FB;

		logf("powering down controller ...\n");
		*REG32(SH_VDD) = 0;
		*REG32(SH_HCFG) = 0;
		*REG32(SH_HBCT) = 0x400;
		*REG32(SH_HBLC) = 0;
		*REG32(SH_HSTS) = 0x7F8;

		logf("resetting state machine ...\n");

		*REG32(SH_CMD) = 0;
		*REG32(SH_ARG) = 0;
	}

  BCM2708SDHost() {
    for (int i=0; i<120; i++) {
      if (restart_controller() == NO_ERROR) {
        logf("eMMC driver sucessfully started!\n");
        return;
      }
      thread_sleep(1000);
    }
  }
};

struct BCM2708SDHost *sdhost = 0;

static ssize_t sdhost_read_block_wrap(struct bdev *bdev, void *buf, bnum_t block, uint count) {
  BCM2708SDHost *dev = reinterpret_cast<BCM2708SDHost*>(bdev);
  TRACEF("sdhost_read_block_wrap(..., 0x%x, %d, %d)\n", (uint32_t)buf, block, count);
  // TODO, wont add right if buf is a 64bit pointer
  uint32_t *dest = reinterpret_cast<uint32_t*>((vaddr_t)buf);
  bool ret;
  for (int retries = 0; retries < 64; retries++) {
    ret = dev->real_read_block(block, dest, count);
    if (ret) break;
  }
  if (!ret) {
    logf("read of sector %d, count %d failed\n", block, count);
    return -1;
  }
  return sdhost->get_block_size() * count;
}

bdev_t *rpi_sdhost_init() {
  if (!sdhost) {
    sdhost = new BCM2708SDHost;
    if (sdhost->card_ready) {
      auto blocksize = sdhost->get_block_size();
      auto blocks = sdhost->capacity_bytes / blocksize;
      bio_initialize_bdev(sdhost, "sdhost", blocksize, blocks, 0, NULL, BIO_FLAGS_NONE);
      //sdhost->read = sdhost_read_wrap;
      sdhost->read_block = sdhost_read_block_wrap;
      bio_register_device(sdhost);
      partition_publish("sdhost", 0);
    } else {
      puts("no SD card found, ignoring SD interface");
      delete sdhost;
      sdhost = NULL;
    }
  }
  return sdhost;
}

void rpi_sdhost_set_clock(uint32_t clock_div) {
  if (sdhost) {
    sdhost->set_clock_div(clock_div);
  }
}

static void sdhost_init(uint level) {
  sd = rpi_sdhost_init();
  printf("%p\n", sd);
}

LK_INIT_HOOK(sdhost, sdhost_init, LK_INIT_LEVEL_PLATFORM + 1);
