.data
bcm2836_payload_start:
  .incbin "../../../build-rpi2-test/lk.bin"
bcm2836_payload_end:

bcm2837_payload_start:
  .incbin "../../../build-rpi3-test/lk.bin"
bcm2837_payload_end:

.data
.global arm_payload_array
arm_payload_array:
  # bcm2835 pi0/pi1
  .int 0
  .int 0
  # bcm2836 pi2
  .int bcm2836_payload_start
  .int bcm2836_payload_end - bcm2836_payload_start
  # bcm2837 pi2 rev1.2 and pi3 in 64bit mode
  .int bcm2837_payload_start
  .int bcm2837_payload_end - bcm2837_payload_start
