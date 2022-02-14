Everything is licensed under the GPLv2 or later unless stated otherwise

# developing with this fork:
```
[clever@system76:~/apps/rpi]$ git clone --recurse-submodules git@github.com:librerpi/lk-overlay.git
[clever@system76:~/apps/rpi]$ cd lk-overlay/
[clever@system76:~/apps/rpi/lk-overlay]$ nix-shell -A shell
[nix-shell:~/apps/rpi/lk-overlay]$ make PROJECT=rpi3-bootcode
[nix-shell:~/apps/rpi/lk-overlay]$ ls -lh build-rpi3-bootcode/lk.bin
-rwxr-xr-x 1 clever users 113K Mar 31 23:27 build-rpi3-bootcode/lk.bin
```

# what features work

| Feature                                    | rpi1 | rpi2 | rpi3 | rpi4 |
| ------------------------------------------ | ---- | ---- | ---- | ---- |
| composite NTSC video                       | ?    | [x]  | ?    | ?    |
| DSI video                                  | [ ]  | [ ]  | [ ]  | [ ]  |
| HDMI video                                 | [ ]  | [ ]  | [ ]  | [ ]  |
| DPI video, partially tested                | ?    | [x]  | ?    | ?    |
| v3d partially working                      | ?    | [x]  | ?    | ?    |
| full 2d composition under firmware control | ?    | [x]  | ?    | ?    |
| CSI, untested                              | [ ]  | [ ]  | [ ]  | [ ]  |
| i2c host                                   | ?    | [x]  | ?    | ?    |
| SPI, untested                              | [ ]  | [ ]  | [ ]  | [ ]  |
| ISP, lacking code                          | [ ]  | [ ]  | [ ]  | [ ]  |
| video decode accel, lacking code           | [ ]  | [ ]  | [ ]  | [ ]  |
| PWM audio, lacking code                    | [ ]  | [ ]  | [ ]  | [ ]  |
| booting linux                              | ?    | [x]  | ?    | ?    |

# projects:
## rpi1-test
builds LK for the armv6 found on the pi0 and pi1

currently not working on this overlay

## rpi2-test
builds LK for the cortex-A7 found in a pi2

use lk.bin as your kernel.img file, with the official firmware

## rpi3-test
builds LK in aarch64 mode for the cortex-A53 found in a pi3

use lk.bin as your kernel.img file, with the official firmware

## rpi3-bootcode
builds LK for the VPU on any VC4 pi (pi0 to pi3), use lk.bin as bootcode.bin

## rpi3-start
buids LK for the VPU on any VC4 pi (pi0 to pi3), use lk.elf as start.elf, with either the official bootcode.bin or vc4-stage1

## rpi4-recovery
builds LK for the VPU on a VC6 pi (pi4, pi400, CM4), use lk.bin as recovery.bin or bootcode.bin in spi flash, must be signed with https://github.com/librerpi/rpi-tools/tree/master/signing-tool

## rpi4-start4
builds LK for the VPU on a VC6 pi (pi4, pi400, CM4), use lk.elf as start4.elf, with the official SPI firmware

## vc4-stage1
builds LK as a bootcode.bin replacement, loads lk.elf from either an ext2 partition or xmodem over the uart

## vc4-stage2
a test stage2 for use with vc4-stage1, currently it just brings the DPI online

## vc4-start
a test stage2, that will embed rpi1-test into itself, and run LK on both the VPU and ARM
