{
  inputs = {
    self.submodules = true;
  };
  outputs = { self, nixpkgs }:
  {
    packages.x86_64-linux = let
      legacy = import ./. {};
      pkgs = import nixpkgs { system = "x86_64-linux"; };
    in {
      vc4-stage1 = legacy.vc4.vc4.stage1;
      vc4-stage2 = legacy.vc4.vc4.stage2;
      rpi2-test = legacy.arm.rpi2-test;
      stage1-bad-apple = legacy.vc4.stage1-bad-apple;
      disk_image = legacy.disk_image;
      mkimage = pkgs.callPackage ./mkimage {};
    };
    hydraJobs.x86_64-linux = {
      inherit (self.packages.x86_64-linux) vc4-stage1 vc4-stage2 rpi2-test stage1-bad-apple disk_image;
    };
  };
}
