{
  description = "C++ CMake Projekt mit SQLite, libarchive und toml++";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        devShells.default = pkgs.mkShell {
          # Werkzeuge, die zum Kompilieren auf dem Host benötigt werden
          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
            gcc          # oder llvmPackages.clang für Clang
          ];

          # Bibliotheken, gegen die gelinkt werden soll
          buildInputs = with pkgs; [
            sqlite
            libarchive
            tomlplusplus # In nixpkgs heißt das Paket tomlplusplus
          ];

          shellHook = ''
            echo "⚡ C++ Entwicklungsumgebung mit CMake geladen! ⚡"
            echo "Verfügbare Bibliotheken: SQLite, libarchive, toml++"
          '';
        };
      });
}

