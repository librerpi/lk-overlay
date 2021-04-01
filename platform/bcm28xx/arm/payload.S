.data
.global arm_payload_start
arm_payload_start:
.incbin "../../../build-rpi1-test/lk.bin"
.global arm_payload_end
arm_payload_end:
