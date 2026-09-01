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
            gcc
            # kernel build dependencies
            bc
            bison
            flex
            arch-install-scripts
            # ISO / bootable media
            grub2
            xorriso
            squashfsTools
            cpio
            # OpenRC build (meson/ninja)
            meson
            ninja
          ];

          # Bibliotheken, gegen die gelinkt werden soll
          buildInputs = with pkgs; [
            sqlite
            libarchive
            tomlplusplus
            openssl      # Für Signatur-Verifikation
            curl         # libcurl transport für Downloads
            python3     # nur für den Integrationstest-Fixture-Server
            # kernel build dependencies
            elfutils     # libelf für kernel modules
            perl         # kernel build scripts
          ];

          shellHook = ''
            echo "meowOS dev shell: SQLite + libarchive + toml++ + openssl"
          '';
        };
      });
}

