{
  description = "ZMK firmware build env for dgct/zmk-config-corne";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    zephyr.url = "github:zmkfirmware/zephyr/v4.1.0+zmk-fixes";
    zephyr.flake = false;
    zephyr-nix.url = "github:urob/zephyr-nix";
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";
    zephyr-nix.inputs.zephyr.follows = "zephyr";
  };

  outputs = { self, nixpkgs, zephyr-nix, ... }:
    let
      forEachSystem = nixpkgs.lib.genAttrs [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
    in {
      devShells = forEachSystem (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          zephyr = zephyr-nix.packages.${system};
        in {
          default = pkgs.mkShell {
            packages = [
              (zephyr.pythonEnv.override { extraPackages = ps: [ ps.west ]; })
              (zephyr.sdk.override { targets = [ "arm-zephyr-eabi" ]; })
              pkgs.cmake
              pkgs.dtc
              pkgs.ninja
              pkgs.gcc
              pkgs.just
              pkgs.yq-go
              pkgs.git
            ];
          };
        });
    };
}
