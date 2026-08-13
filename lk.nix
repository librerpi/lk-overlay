{ lib, stdenv, project, src, lkSrc, tinyusbSrc, vc4-toolchain, vc4Gcc, vc4Newlib, which, imagemagick
, python3, vc4Toolchain ? "llvm", preBuild ? "", armstubs ? null
, armImages ? {}, fetchFromGitHub }:

let
  lwip = fetchFromGitHub {
    owner = "lwip-tcpip";
    repo = "lwip";
    rev = "73fcf72792a926a4e0ac8b656b29bff70552d927";
    hash = "sha256-ImeKwXtErpYO0vWu8w9VJhuDR2eJzKdfowYTKuRKkvs=";
  };
in
stdenv.mkDerivation ({
  name = "littlekernel-${project}";
  inherit src;
  postUnpack = ''
    chmod -R u+w "$sourceRoot"
    rm -rf "$sourceRoot/lk" "$sourceRoot/lib/tinyusb/upstream"
    cp -r ${lkSrc} "$sourceRoot/lk"
    mkdir -p "$sourceRoot/lib/tinyusb"
    cp -r ${tinyusbSrc} "$sourceRoot/lib/tinyusb/upstream"
    chmod -R u+w "$sourceRoot/lk" "$sourceRoot/lib/tinyusb/upstream"
    rm -rf $sourceRoot/lk/external/lib/lwip/upstream
    cp -r ${lwip} $sourceRoot/lk/external/lib/lwip/upstream
  '';
  preBuild = ''
    ${preBuild}
    ${lib.concatStringsSep "\n" (lib.mapAttrsToList (name: image:
      ''
        mkdir -p "build-${name}"
        ln -s ${image}/lk.bin "build-${name}/lk.bin"
      '') armImages)}
  '';
  makeFlags = [ "PROJECT=${project}" ];
  hardeningDisable = [ "format" ];
  nativeBuildInputs = [
    which
    imagemagick.__spliced.buildBuild # work around a bug in nixpkgs
    python3
  ] ++ lib.optional (stdenv.hostPlatform.config == "vc4-elf")
    (if vc4Toolchain == "gcc" then vc4Gcc else vc4-toolchain);
  installPhase = ''
    mkdir -p $out/nix-support
    cp -r build-${project}/{config.h,lk.*} $out
    cat <<EOF > $out/nix-support/hydra-metrics
    lk.bin $(stat --printf=%s $out/lk.bin) bytes
    lk.elf $(stat --printf=%s $out/lk.elf) bytes
    EOF
    echo "file binary-dist $out/lk.bin" >> $out/nix-support/hydra-build-products
    echo "file binary-dist $out/lk.elf" >> $out/nix-support/hydra-build-products
  '';
  ARCH_arm64_TOOLCHAIN_PREFIX = "aarch64-none-elf-";
} // lib.optionalAttrs (armstubs != null) {
  ARMSTUBS = armstubs;
} // lib.optionalAttrs (stdenv.hostPlatform.config == "vc4-elf") ({
  VC4_TOOLCHAIN = vc4Toolchain;
} // (if vc4Toolchain == "gcc" then {
} else {
  LLVM_BINDIR = "${vc4-toolchain}/bin";
  VC4_SYSROOT = "${vc4Newlib}/vc4-elf";
  LIBGCC = "${vc4-toolchain}/lib/libclang_rt.builtins-videocore.a";
}))
)
