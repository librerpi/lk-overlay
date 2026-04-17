all: fixed-point fixed-point-vpu fixed-point-arm32-pi1 fixed-point-arm32-pi2 fixed-point-arm32-pi3 fixed-point-arm32-pi4 fixed-point-arm32-pi3-thumb fixed-point-aarch64

CFLAGS=-Wall

util.S: util.cpp fixed-point.h util.h
	g++ -S $< -o $@ ${CFLAGS} -O3

util-arm32.S: util.cpp fixed-point.h util.h
	arm-none-eabi-g++ -S $< -o $@ ${CFLAGS} -O3

util-vpu.S: util.cpp fixed-point.h util.h
	vc4-elf-g++ -S $< -o $@ ${CFLAGS} -O3

fixed-point: fixed-point.cpp util.cpp fixed-point.h
	g++ $< util.cpp -o $@ ${CFLAGS}
	objdump -d $@ | c++filt > $@.dis

examples: examples.cpp util.cpp fixed-point.h util.h
	g++ $< util.cpp -o $@ ${CFLAGS}

fixed-point-vpu: fixed-point.cpp
	vc4-elf-g++ $< -o $@ ${CFLAGS} -c -O3
	vc4-elf-objdump -dr $@ | c++filt > $@.dis

fixed-point-arm32-pi1: fixed-point.cpp
	arm-none-eabi-g++ $< -o $@ ${CFLAGS} -c -mfpu=vfp -march=armv6zk -mtune=arm1176jzf-s -O3
	arm-none-eabi-objdump -dr $@ | c++filt > $@.dis
fixed-point-arm32-pi2: fixed-point.cpp
	arm-none-eabi-g++ $< -o $@ ${CFLAGS} -c -mfpu=neon-vfpv4 -march=armv7-a -mtune=cortex-a7 -O3
	arm-none-eabi-objdump -dr $@ | c++filt > $@.dis
fixed-point-arm32-pi3: fixed-point.cpp
	arm-none-eabi-g++ $< -o $@ ${CFLAGS} -c -mfpu=crypto-neon-fp-armv8 -march=armv8-a+crc -mcpu=cortex-a53 -O3
	arm-none-eabi-objdump -dr $@ | c++filt > $@.dis
fixed-point-arm32-pi3-thumb: fixed-point.cpp
	arm-none-eabi-g++ $< -o $@ ${CFLAGS} -c -mfpu=crypto-neon-fp-armv8 -march=armv8-a+crc -mcpu=cortex-a53 -O3 -mthumb
	arm-none-eabi-objdump -dr $@ | c++filt > $@.dis
fixed-point-arm32-pi4: fixed-point.cpp
	arm-none-eabi-g++ $< -o $@ ${CFLAGS} -c -mfpu=crypto-neon-fp-armv8 -march=armv8-a+crc -mcpu=cortex-a72
	arm-none-eabi-objdump -dr $@ | c++filt > $@.dis

fixed-point-aarch64: fixed-point.cpp
	aarch64-none-elf-g++ $< -o $@ ${CFLAGS} -c -O3
	aarch64-none-elf-objdump -dr $@ | c++filt > $@.dis
