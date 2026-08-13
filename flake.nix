{
  description = "Little Kernel firmware and the LLVM VC4/VCE toolchain";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    llvm-project = {
      url = "github:Vali0004/llvm-project-rpi/main";
      flake = false;
    };

    lk-src = {
      url = "github:librerpi/lk/3aa75cf8dcdf5694da549311a0a8cb456a8e8228";
      flake = false;
    };

    tinyusb-src = {
      url = "github:hathach/tinyusb/51a0889b75fc2da6675530d68f3ae765c44849f4";
      flake = false;
    };

    rpi-open-firmware = {
      url = "github:librerpi/rpi-open-firmware";
      flake = false;
    };
  };

  outputs = inputs@{ self, nixpkgs, ... }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
    packages = import ./nix/packages.nix {
      inherit inputs pkgs self;
      src = self;
    };
  in {
    packages.${system} = packages;

    checks.${system} = {
      inherit (packages) vc4-stage1 vc4-stage2 rpi2-test;
    };

    hydraJobs.${system} = {
      inherit (packages) vc4-stage1 vc4-stage2 rpi2-test stage1-bad-apple disk_image dist dist_deb;
      inherit (packages) vc4-stage1-gcc vc4-stage2-gcc;
      vc4-toolchain-llvm = packages.vc4-toolchain;
      vc4-toolchain-gcc = packages.vc4-gcc;
    };

    devShells.${system}.default = pkgs.mkShell {
      inputsFrom = [ packages.vc4-toolchain ];
      packages = with pkgs; [
        bison
        flex
        imagemagick
        libpng
        nlohmann_json
        python3
        sox
        packages.vc4-gcc
      ];
      LLVM_BINDIR = "${packages.vc4-toolchain}/bin";
      VC4_SYSROOT = "${packages.vc4-gcc.libc}/vc4-elf";
      LIBGCC = "${packages.vc4-toolchain}/lib/libclang_rt.builtins-videocore.a";
      VC4_GCC_BINDIR = "${packages.vc4-gcc}/bin";
    };

    formatter.${system} = pkgs.nixfmt-tree;
  };
}
