{ stdenv, nlohmann_json }:

stdenv.mkDerivation {
  name = "mkimage";
  src = ./.;
  buildInputs = [ nlohmann_json ];
  patchPhase = ''
    cp -r ${../lk/external/lib/mincrypt} mincrypt
  '';
}
