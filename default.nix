{ risc ? false }:

let
  sources = import ./nix/sources.nix;
  pkgs = import (builtins.fetchTarball {
    url = "https://github.com/input-output-hk/nixpkgs/archive/0ee0489d42e.tar.gz";
    sha256 = "1ldlg2nm8fcxszc29rngw2893z8ci4bpa3m0i6kfwjadfrcrfa42";
  }) { system = "x86_64-linux"; };
  lib = pkgs.lib;
  rpi-tools = pkgs.fetchFromGitHub {
    owner = "raspberrypi";
    repo = "tools";
    rev = "439b6198a9b340de5998dd14a26a0d9d38a6bcac";
    hash = "sha256-rcrVDSi5wArStnCm5kUtOzlw64WVDl7fV94/aQu77Qg=";
  };
  overlay = self: super: {
    littlekernel = self.stdenv.mkDerivation {
      name = "littlekernel";
      src = lib.cleanSource ./.;
      #nativeBuildInputs = [ x86_64.uart-manager ];
      nativeBuildInputs = [ x86_64.python x86_64.imagemagick x86_64.qemu ];
      hardeningDisable = [ "format" ];
    };
    uart-manager = self.stdenv.mkDerivation {
      name = "uart-manager";
      src = sources.rpi-open-firmware + "/uart-manager";
    };
  };
  vc4 = pkgs.pkgsCross.vc4.extend overlay;
  x86_64 = pkgs.extend overlay;
  arm7 = pkgs.pkgsCross.arm-embedded.extend overlay;
in lib.fix (self: {
  # https://leiradel.github.io/2019/01/20/Raspberry-Pi-Stubs.html
  # armstub.bin, for the bcm2835, armv6 mode
  # armstub7.bin for the bcm2836, armv7 mode, parks secondary cores waiting for mail from primary core
  # armstub8-32.bin for the bcm2837, 32bit armv7 mode, parks secondary cores
  # armstub8.bin for the bcm2837, 64bit armv8 mode
  #   0xf0 the magic 0x5afe570b
  #   0xf4 the stub version
  #   0xf8 atags/FDT addr
  #   0xfc kernel entry-point
  armstubs = pkgs.runCommand "armstubs" {
    src = "${rpi-tools}/armstubs";
    buildInputs = with pkgs; [
      pkgsCross.arm-embedded.stdenv.cc
      pkgsCross.aarch64-embedded.stdenv.cc
    ];
    CC7 = "arm-none-eabi-gcc -march=armv7-a";
    LD7 = "arm-none-eabi-ld";
    OBJCOPY7 = "arm-none-eabi-objcopy";
    CC8 = "aarch64-none-elf-gcc";
    LD8 = "aarch64-none-elf-ld";
    OBJCOPY8 = "aarch64-none-elf-objcopy";
  } ''
    echo $buildInputs
    unpackPhase
    cd $sourceRoot
    make
    mkdir $out
    cp *.bin $out/
  '';
  shell = pkgs.stdenv.mkDerivation {
    name = "shell";
    buildInputs = with pkgs; [
      pkgsCross.arm-embedded.stdenv.cc
      #pkgsCross.i686-embedded.stdenv.cc
      pkgsCross.vc4.stdenv.cc
      pkgsCross.aarch64-embedded.stdenv.cc
      python
      python3
      qemu
      imagemagick
      pkgsi686Linux.lua
    ] ++ lib.optionals risc [
      pkgsCross.riscv32-embedded.stdenv.cc
      pkgsCross.riscv64-embedded.stdenv.cc
    ];
    ARCH_x86_TOOLCHAIN_PREFIX = "i686-elf-";
    ARCH_x86_TOOLCHAIN_INCLUDED = true;
    ARCH_arm64_TOOLCHAIN_PREFIX = "aarch64-none-elf-";
    ARMSTUBS = self.armstubs;
  };
  roots = pkgs.writeText "gc-roots" ''
    ${pkgs.pkgsCross.vc4.stdenv.cc}"
  '';
  arm7 = {
    inherit (arm7) littlekernel;
  };
  arm = {
    rpi1-test = arm7.callPackage ./lk.nix { project = "rpi1-test"; };
    rpi2-test = arm7.callPackage ./lk.nix { project = "rpi2-test"; };
    rpi3-test = pkgs.pkgsCross.aarch64-embedded.callPackage ./lk.nix { project = "rpi3-test"; };
  };
  vc4 = {
    shell = vc4.littlekernel;
    rpi3.bootcode = vc4.callPackage ./lk.nix { project = "rpi3-bootcode"; };
    rpi3.start = vc4.callPackage ./lk.nix { project = "rpi3-start"; };
    rpi4.recovery = vc4.callPackage ./lk.nix { project = "rpi4-recovery"; };
    rpi4.start4 = vc4.callPackage ./lk.nix { project = "rpi4-start4"; };
    vc4.stage1 = vc4.callPackage ./lk.nix { project = "vc4-stage1"; };
    vc4.stage2 = vc4.callPackage ./lk.nix {
      project = "vc4-stage2";
      preBuild = ''
        rm -rf build-rpi{1,2,3}-test
        mkdir build-rpi{1,2,3}-test
        ln -sv ${self.arm.rpi1-test}/lk.bin build-rpi1-test/lk.bin
        ln -sv ${self.arm.rpi2-test}/lk.bin build-rpi2-test/lk.bin
        ln -sv ${self.arm.rpi3-test}/lk.bin build-rpi3-test/lk.bin
      '';
      extraAttrs = {
        ARMSTUBS = self.armstubs;
      };
    };
    vc4.start = vc4.callPackage ./lk.nix {
      project = "vc4-start";
      preBuild = ''
        rm -rf build-rpi2-test build-rpi3-test
        mkdir build-rpi2-test build-rpi3-test -pv
        ln -sv ${self.arm.rpi2-test}/lk.bin build-rpi2-test/lk.bin
        ln -sv ${self.arm.rpi3-test}/lk.bin build-rpi3-test/lk.bin
      '';
    };
    vc4.bootcode-fast-ntsc = vc4.callPackage ./lk.nix { project = "bootcode-fast-ntsc"; };
  };
  x86_64 = {
    inherit (x86_64) uart-manager;
  };
  testcycle = pkgs.writeShellScript "testcycle" ''
    set -e
    scp ${self.vc4.rpi3.bootcode}/lk.bin root@router.localnet:/tftproot/open-firmware/bootcode.bin
    exec ${x86_64.uart-manager}/bin/uart-manager
  '';
  disk_image = pkgs.vmTools.runInLinuxVM (pkgs.runCommand "disk-image" {
    buildInputs = with pkgs; [ utillinux dosfstools e2fsprogs mtools libfaketime ];
    preVM = ''
      mkdir -p $out
      diskImage=$out/disk-image.img
      truncate $diskImage -s 64m
    '';
    postVM = ''
    '';
  } ''
    sfdisk /dev/vda <<EOF
    label: dos
    label-id: 0x245a585c
    unit: sectors

    1: size=${toString (32 * 2048)}, type=c
    2: type=83
    EOF

    mkdir ext-dir
    cp ${self.vc4.vc4.stage2}/lk.elf ext-dir/lk.elf -v

    faketime "1970-01-01 00:00:00" mkfs.fat /dev/vda1 -i 0x2178694e
    mkfs.ext2 /dev/vda2 -d ext-dir

    mkdir fat-dir
    cp -v ${self.vc4.vc4.stage1}/lk.bin fat-dir/bootcode.bin
    cd fat-dir
    faketime "1970-01-01 00:00:00" mcopy -psvm -i /dev/vda1 * ::
  '');
})
