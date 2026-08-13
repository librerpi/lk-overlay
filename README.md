
![](./librerpi-without-text.png)

Everything is licensed under the GPLv2 or later unless stated otherwise

# developing with this lk overlay

The flake uses the VC4/VCE-enabled LLVM fork by default, while retaining the
historical VC4 GCC toolchain for one-to-one compiler comparisons.

```console
$ nix develop
$ make PROJECT=vc4-stage1 VC4_TOOLCHAIN=llvm
$ make PROJECT=vc4-stage1 VC4_TOOLCHAIN=gcc
```

The development shell supplies LLVM, its VideoCore compiler-rt builtins, the
VC4 newlib sysroot, and `vc4-elf-*` GCC tools. `VC4_TOOLCHAIN` may be set on
the command line or in the environment; it defaults to `llvm`.

For reproducible builds which do not modify the working tree:

```console
$ nix build .#vc4-stage1       # LLVM
$ nix build .#vc4-stage1-gcc   # legacy GCC baseline
$ nix build .#vc4-toolchain    # LLVM/Clang with VC4 and VCE backends
$ nix build .#vc4-gcc          # standalone legacy GCC toolchain
```

# what features work

| Feature                                                  | rpi1 | rpi2 | rpi3 | rpi4 |
| -------------------------------------------------------- | ---- | ---- | ---- | ---- |
| composite NTSC video                                     | [x]  | [x]  | [x]  | ?    |
| DSI video                                                | [ ]  | [ ]  | [ ]  | [ ]  |
| HDMI video                                               | [x]  | [x]  | [x]  | [ ]  |
| DPI video, partially tested                              | [x]  | [x]  | [x]  | ?    |
| v3d                                                      | [x]  | [x]  | [x]  | ?    |
| full 2d composition under firmware control               | [x]  | [x]  | [x]  | ?    |
| CSI, untested                                            | [ ]  | [ ]  | [ ]  | [ ]  |
| i2c host (under linux)                                   | ?    | [x]  | [x]  | ?    |
| SPI, untested                                            | [ ]  | [ ]  | [ ]  | [ ]  |
| ISP, lacking code                                        | [ ]  | [ ]  | [ ]  | [ ]  |
| video decode accel, lacking code                         | [ ]  | [ ]  | [ ]  | [ ]  |
| PWM audio, lacking code                                  | [ ]  | [ ]  | [ ]  | [ ]  |
| booting Linux                                            | ?    | [x]  | ?    | ?    |
| Ethernet (including mac address from rpi serial number)  | ?    | [x]  | ?    | ?    |
| USB host (under linux)                                   | ?    | [x]  | ?    | ?    |
| SD/MicroSD: works 95% of the time                        | ?    | [x]  | ?    | ?    |

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
