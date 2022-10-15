#include <lk/asm.h>

.section .rodata
bcm2835_payload_start:
  .incbin "../../../build-rpi1-test/lk.bin"
bcm2835_payload_end:

bcm2836_payload_start:
  .incbin "../../../build-rpi2-test/lk.bin"
bcm2836_payload_end:

bcm2837_payload_start:
  .incbin "../../../build-rpi3-test/lk.bin"
bcm2837_payload_end:

armstub_start:
  .incbin "armstub.bin"
armstub_end:

armstub7_start:
  .incbin "armstub7.bin"
armstub7_end:

armstub8_32_start:
  .incbin "armstub8-32.bin"
armstub8_32_end:

armstub8_start:
  .incbin "armstub8.bin"
armstub8_end:

.data
DATA(arm_payload_array)
arm_payload_array:
  # bcm2835 pi0/pi1
  .int bcm2835_payload_start
  .int bcm2835_payload_end - bcm2835_payload_start
  # bcm2836 pi2
  .int bcm2836_payload_start
  .int bcm2836_payload_end - bcm2836_payload_start
  # bcm2837 pi2 rev1.2 and pi3 in 64bit mode
  .int bcm2837_payload_start
  .int bcm2837_payload_end - bcm2837_payload_start
END_DATA(arm_payload_array)

.data
DATA(armstub_array)
armstub_array:
  # bcm2835 pi0/pi1 armv6
  .int armstub_start
  .int armstub_end - armstub_start
  .int 0
  .int 32
  # bcm2836 pi2 armv7
  .int armstub7_start
  .int armstub7_end - armstub7_start
  .int 1
  .int 32
  # bcm2837 pi3/pi02 armv7
  .int armstub8_32_start
  .int armstub8_32_end - armstub8_32_start
  .int 2
  .int 32
  # bcm2837 pi3/pi02 aarch64
  .int armstub8_start
  .int armstub8_end - armstub8_start
  .int 2
  .int 64
END_DATA(armstub_array)
