{ lib, stdenv, cmake, ninja, python3, llvm-project, llvmPackages_latest
, symlinkJoin }:

let
  llvm = stdenv.mkDerivation {
    pname = "llvm-vc4-vce";
    version = "22.0.0git";
    src = llvm-project;
    sourceRoot = "source/llvm";

    nativeBuildInputs = [ cmake ninja python3 ];
    buildInputs = [ llvmPackages_latest.llvm ];

    cmakeFlags = [
      (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
      (lib.cmakeBool "CMAKE_BUILD_WITH_INSTALL_RPATH" true)
      (lib.cmakeFeature "LLVM_ENABLE_PROJECTS" "clang;lld")
      (lib.cmakeFeature "LLVM_TARGETS_TO_BUILD" "X86")
      (lib.cmakeFeature "LLVM_EXPERIMENTAL_TARGETS_TO_BUILD" "Videocore;VCE")
      (lib.cmakeBool "LLVM_ENABLE_ASSERTIONS" true)
      (lib.cmakeBool "LLVM_INSTALL_TOOLCHAIN_ONLY" true)
      (lib.cmakeFeature "LLVM_TOOLCHAIN_TOOLS"
        "clang;lld;llvm-ar;llvm-cxxfilt;llvm-mc;llvm-nm;llvm-objcopy;llvm-objdump;llvm-ranlib;llvm-readobj;llvm-size;llvm-strip")
    ];

    postInstall = ''
      ln -sfn clang "$out/bin/videocore-clang"
      ln -sfn ld.lld "$out/bin/videocore-ld"
    '';

    meta = {
      description = "LLVM, Clang, and LLD with experimental VideoCore IV and VCE backends";
      platforms = [ "x86_64-linux" ];
    };
  };

  builtins = stdenv.mkDerivation {
    pname = "compiler-rt-builtins-videocore";
    inherit (llvm) version;
    src = llvm-project;
    sourceRoot = "source/compiler-rt";

    nativeBuildInputs = [ cmake ninja python3 llvm ];

    cmakeFlags = [
      "-GNinja"
      (lib.cmakeFeature "CMAKE_BUILD_TYPE" "Release")
      (lib.cmakeFeature "CMAKE_C_COMPILER" "${llvm}/bin/clang")
      (lib.cmakeFeature "CMAKE_ASM_COMPILER" "${llvm}/bin/clang")
      (lib.cmakeFeature "CMAKE_C_COMPILER_TARGET" "videocore-unknown-unknown-elf")
      (lib.cmakeFeature "CMAKE_ASM_COMPILER_TARGET" "videocore-unknown-unknown-elf")
      (lib.cmakeFeature "CMAKE_TRY_COMPILE_TARGET_TYPE" "STATIC_LIBRARY")
      (lib.cmakeFeature "CMAKE_AR" "${llvm}/bin/llvm-ar")
      (lib.cmakeFeature "CMAKE_RANLIB" "${llvm}/bin/llvm-ranlib")
      (lib.cmakeBool "COMPILER_RT_BAREMETAL_BUILD" true)
      (lib.cmakeBool "COMPILER_RT_BUILD_BUILTINS" true)
      (lib.cmakeBool "COMPILER_RT_DEFAULT_TARGET_ONLY" true)
      (lib.cmakeFeature "COMPILER_RT_DEFAULT_TARGET_ARCH" "videocore")
    ];

    buildPhase = ''
      runHook preBuild
      ninja builtins
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      mkdir -p "$out/lib"
      cp lib/linux/libclang_rt.builtins-videocore.a "$out/lib/"
      runHook postInstall
    '';
  };
in symlinkJoin {
  name = "vc4-toolchain-${llvm.version}";
  paths = [ llvm builtins ];
  passthru = { inherit llvm builtins; };
}
