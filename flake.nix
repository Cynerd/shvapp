{
  description = "Silicon Heaven Application's Flake";

  outputs = {
    self,
    systems,
    nixpkgs,
  }: let
    inherit (nixpkgs.lib) genAttrs;
    forSystems = genAttrs (import systems);
    withPkgs = func: forSystems (system: func self.legacyPackages.${system});
    rev = self.shortRev or self.dirtyShortRev or "unknown";

    package = {
      stdenv,
      qt6Packages,
      libxkbcommon,
      doctest,
      lua5_3,
      cmake,
    }:
      stdenv.mkDerivation {
        name = "shvapp-${rev}";
        src = ./.;
        outputs = ["out" "dev"];
        buildInputs = [
          qt6Packages.wrapQtAppsHook
          qt6Packages.qtbase
          qt6Packages.qtmqtt
          qt6Packages.qtnetworkauth
          qt6Packages.qtquick3d
          qt6Packages.qtserialport
          qt6Packages.qtsvg
          qt6Packages.qtwebsockets
          libxkbcommon
          doctest
          lua5_3
        ];
        nativeBuildInputs = [
          cmake
          qt6Packages.qttools
        ];
      };
  in {
    nixosModules = import ./nixos/modules self.overlays.default;
    overlays.default = final: _: {
      shvapp = final.callPackage package {};
    };

    packages = withPkgs (pkgs: {default = pkgs.shvapp;});

    legacyPackages =
      forSystems (system:
        nixpkgs.legacyPackages.${system}.extend self.overlays.default);

    formatter = withPkgs (pkgs: pkgs.alejandra);
  };
}
