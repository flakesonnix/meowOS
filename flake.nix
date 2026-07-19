{
  description = "meowOS - Linux package manager";

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
            openssl      # Für Signatur-Verifikation
          ];

          shellHook = ''
            echo "meowOS dev shell: SQLite + libarchive + toml++ + openssl"
          '';
        };
      });
}

