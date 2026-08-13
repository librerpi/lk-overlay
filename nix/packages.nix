{ inputs, pkgs, self, src }:

let
  lib = pkgs.lib;

  # Keep the historical VC4 GCC/binutils stack as the known comparison
  # baseline. Current nixpkgs no longer builds its patched VC4 binutils.
  legacyPkgs = import (builtins.fetchTarball {
    url = "https://github.com/NixOS/nixpkgs/archive/ce4c65a127ce4d29b469672ec06cb60ca776156b.tar.gz";
    sha256 = "0lmhzbbqvrbnbd57mz47kzkckhgyijwvxlfszvxvl3rvyaw6z5yz";
  }) { system = "x86_64-linux"; };

  vc4-toolchain = pkgs.callPackage ./vc4-toolchain.nix {
    llvm-project = inputs.llvm-project;
  };
  vc4Gcc = legacyPkgs.pkgsCross.vc4.stdenv.cc;
  vc4Newlib = legacyPkgs.pkgsCross.vc4.stdenv.cc.libc;

  rpi-tools = pkgs.fetchFromGitHub {
    owner = "raspberrypi";
    repo = "tools";
    rev = "439b6198a9b340de5998dd14a26a0d9d38a6bcac";
    hash = "sha256-rcrVDSi5wArStnCm5kUtOzlw64WVDl7fV94/aQu77Qg=";
  };

  armstubs = pkgs.runCommand "armstubs" {
    src = "${rpi-tools}/armstubs";
    nativeBuildInputs = [
      pkgs.pkgsCross.arm-embedded.stdenv.cc
      pkgs.pkgsCross.aarch64-embedded.stdenv.cc
    ];
    CC7 = "arm-none-eabi-gcc -march=armv7-a";
    LD7 = "arm-none-eabi-ld";
    OBJCOPY7 = "arm-none-eabi-objcopy";
    CC8 = "aarch64-none-elf-gcc";
    LD8 = "aarch64-none-elf-ld";
    OBJCOPY8 = "aarch64-none-elf-objcopy";
  } ''
    unpackPhase
    cd "$sourceRoot"
    make
    mkdir -p "$out"
    cp *.bin "$out/"
  '';

  mkLk = packageSet: project: args:
    packageSet.callPackage ../lk.nix ({
      inherit project src vc4-toolchain;
      inherit vc4Gcc vc4Newlib;
      lkSrc = inputs.lk-src;
      tinyusbSrc = inputs.tinyusb-src;
    } // args);

  arm = rec {
    rpi1-test = mkLk pkgs.pkgsCross.arm-embedded "rpi1-test" { };
    rpi2-test = mkLk pkgs.pkgsCross.arm-embedded "rpi2-test" { };
    rpi3-test = mkLk pkgs.pkgsCross.aarch64-embedded "rpi3-test" { };
  };

  vc4 = rec {
    rpi3-bootcode = mkLk legacyPkgs.pkgsCross.vc4 "rpi3-bootcode" { };
    rpi3-start = mkLk legacyPkgs.pkgsCross.vc4 "rpi3-start" { };
    rpi4-recovery = mkLk legacyPkgs.pkgsCross.vc4 "rpi4-recovery" { };
    rpi4-start4 = mkLk legacyPkgs.pkgsCross.vc4 "rpi4-start4" { };
    stage1-bad-apple = mkLk legacyPkgs.pkgsCross.vc4 "stage1-bad-apple" { };
    vc4-stage1 = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1" { };
    vc4-stage1-netonly = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1-netonly" { };
    vc4-stage1-usbonly = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1-usbonly" { };
    vc4-stage1-sdonly = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1-sdonly" { };
    vc4-stage1-spi = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1-spi" { };
    vc4-stage2-spi = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage2-spi" { };
    vc4-stage1-sdusb = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1-sdusb" { };
    vc4-stage2-sdusb = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage2-sdusb" { };
    vc4-stage2 = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage2" {
      inherit armstubs;
      armImages = arm;
    };
    vc4-start = mkLk legacyPkgs.pkgsCross.vc4 "vc4-start" {
      armImages = {
        inherit (arm) rpi2-test rpi3-test;
      };
    };
    vc4-stage1-gcc = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage1" {
      vc4Toolchain = "gcc";
    };
    vc4-stage2-gcc = mkLk legacyPkgs.pkgsCross.vc4 "vc4-stage2" {
      vc4Toolchain = "gcc";
      inherit armstubs;
      armImages = arm;
    };
  };

  disk_image = lib.overrideDerivation
    (pkgs.vmTools.runInLinuxVM (pkgs.runCommand "disk-image" {
      buildInputs = with pkgs; [
        util-linux
        dosfstools
        e2fsprogs
        mtools
        libfaketime
      ];
      preVM = ''
        mkdir -p $out
        diskImage=$out/disk-image.img
        truncate "$diskImage" -s 64m
      '';
      postVM = ''
        mkdir -p $out/nix-support
        echo "file sd-image $out/disk-image.img" > $out/nix-support/hydra-build-products
      '';
    } ''
      sfdisk /dev/vda <<EOF
      label: dos
      label-id: 0x245a585c
      unit: sectors

      1: size=${toString (32 * 2048)}, type=c
      2: type=83
      EOF

      mkdir -p ext-dir/etc ext-dir/boot/firmware
      cp ${vc4.vc4-stage2}/lk.elf ext-dir/boot/lk.elf
      cat > ext-dir/etc/fstab <<EOF
      LABEL=root / defaults 0 0
      LABEL=firmware /boot/firmware defaults 0 0
      EOF

      faketime "1970-01-01 00:00:00" mkfs.fat /dev/vda1 -i 0x2178694e -n firmware
      mkfs.ext4 /dev/vda2 -d ext-dir -L root

      mkdir fat-dir
      cp ${vc4.vc4-stage1}/lk.bin fat-dir/bootcode.bin
      cd fat-dir
      faketime "1970-01-01 00:00:00" mcopy -psvm -i /dev/vda1 * ::
    ''))
    (_: { requiredSystemFeatures = [ ]; });

  zImage = pkgs.fetchurl {
    url = "https://ext.earthtools.ca/private/rpi/zImage-2020-05-19";
    sha256 = "09kijy3rrwzf6zgrq3pbww9267b1dr0s9rippz7ygk354lr3g7c8";
  };

  dist = pkgs.runCommandCC "dist" {
    nativeBuildInputs = [ pkgs.dtc ];
  } ''
    mkdir -p $out/boot/firmware $out/nix-support
    cp ${vc4.vc4-stage1}/lk.bin $out/boot/firmware/bootcode.bin
    cp ${vc4.vc4-stage2}/lk.elf $out/boot/lk.elf

    builddtb() {
      cc -x assembler-with-cpp -E "$1" -o temp
      grep -Ev '^#' temp > temp2
      dtc temp2 -o "$2"
      rm temp temp2
    }

    builddtb ${inputs.rpi-open-firmware}/rpi2.dts $out/boot/rpi2.dtb
    builddtb ${inputs.rpi-open-firmware}/rpi3.dts $out/boot/rpi3.dtb
    echo root=/dev/mmcblk0p2 > $out/boot/cmdline.txt
    cp ${zImage} $out/boot/zImage

    tar -C $out --sort=name -cvf $out/boot.tar boot/
    echo "file binary-dist $out/boot.tar" > $out/nix-support/hydra-build-products
  '';

  dist_deb = pkgs.runCommandCC "dist_deb" {
    nativeBuildInputs = [ pkgs.dpkg ];
  } ''
    cp -r ${../dpkg-input} input
    chmod -R 755 input
    mkdir -p $out/nix-support
    tar -C input -xvf ${dist}/boot.tar
    dpkg-deb --root-owner-group --build input $out/librepi-firmware.deb
    echo "file binary-dist $out/librepi-firmware.deb" > $out/nix-support/hydra-build-products
  '';
in vc4 // arm // {
  inherit armstubs disk_image dist dist_deb vc4-toolchain;
  vc4-gcc = vc4Gcc;
  default = vc4.vc4-stage1;
  mkimage = pkgs.callPackage ../mkimage { };
}
