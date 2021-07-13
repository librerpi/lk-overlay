#include <lk/console_cmd.h>
#include <platform/bcm28xx/otp.h>
#include <stdio.h>

static int cmd_dump_keys(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("dump-keys", "dump boot signing keys", &cmd_dump_keys)
STATIC_COMMAND_END(signing_dump);

static void found_keys(uint32_t *otp, uint8_t *salt) {
  puts("START\nmodule.exports = {\n  \"salt\":[");
  for (int i=0; i<20; i++) {
    printf("0x%02x", salt[i]);
    if (i != 19) printf(",");
    if (i % 4 == 3) puts("");
  }
  puts("],\n\"otp\":[");
  for (int i=0; i<4; i++) {
    printf("0x%08x", otp[i]);
    if (i != 3) printf(",");
    puts("");
  }
  puts("]\n};\nEND");
}

static int cmd_dump_keys(int argc, const console_cmd_args *argv) {
  puts("primary signing key:\n");
  uint32_t otp[4];
  for (int i=0; i<4; i++) {
    otp[i] = otp_read(i + 19);
    printf("OTP(%d) == 0x%08x\n", i+19, otp[i]);
  }
  puts("backup signing key:\n");
  for (int i=0; i<4; i++) {
    uint32_t t = otp_read(i + 23);
    printf("OTP(%d) == 0x%08x\n", i+23, t);
    if (otp[i] != t) {
      printf("mismatch on OTP key word %d!!\n", i);
    }
  }
  uint8_t *o_key_pad = (uint8_t*)0x60010218;
  puts("o_key_pad: ");
  for (int i=0; i<64; i++) {
    printf("0x%02x ", o_key_pad[i]);
    if (i % 16 == 0) puts("");
  }
  if (o_key_pad[63] != 0x5c) {
    puts("\no_key_pad not found in expected sram location, trying B0T maskrom addr");
    uint8_t *rom_salt = (uint8_t*)0x600035a4;
    puts("salt from rom: ");
    for (int i=0; i<20; i++) {
      printf("0x%02x ", rom_salt[i]);
    }
    puts("");
    found_keys(otp, rom_salt);
  } else {
    puts("\nfinal key: ");
    for (int i=0; i<20; i++) {
      printf("0x%02x ", o_key_pad[i] ^ 0x5c);
    }
    uint8_t *otp8 = (uint8_t*)otp;
    puts("\nsalt: ");
    for (int i=0; i<20; i++) {
      printf("0x%02x ", o_key_pad[i] ^ 0x5c ^ otp8[i]);
    }
  }
  return 0;
}
