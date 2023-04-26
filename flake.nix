{
  description = "flux-core";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-22.11";
    flake-utils.url = "github:numtide/flake-utils";
    # flake-compat = {
    #   url = "github:edolstra/flake-compat";
    #   flake = false;
    # };
  };
  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachSystem [
      "aarch64-linux"
      "powerpc64le-linux"
      "x86_64-linux"
    ]
      (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          # get something close to the `git describe` based version that works
          # inside a flake build
          version_base = builtins.head (builtins.match "^flux-core version ([^ ]*) .*" (builtins.readFile "${self}/NEWS.md"));
          version_rev = if self ? "shortRev" then "${self.shortRev}" else "dirty";
          version_revcount = if self ? "revCount" then self.revCount else "dirty";
          version_suffix = if version_revcount == "0" then "" else "-${version_revcount}-${version_rev}";

          baseLua = pkgs.lua5_3;
          basePython = pkgs.python310;
        in
        rec {
          devShells.default = self.packages.${system}.default.overrideAttrs (
            final: prev: {
              # avoid patching scripts in the working copy
              preBuild = ''
                # zsh can cause problems with buildPhase, use something fast
                export SHELL=dash
              '';
            }
          );
          # Special extended development shell with linters and other goodies
          devShells.dev = self.devShells.${system}.default.overrideAttrs (
            final: prev: {
              nativeBuildInputs = prev.nativeBuildInputs ++ (with pkgs; [
                bear

                clang-tools
                pre-commit
              ]) ++ (with basePython.pkgs; [
                black
                mypy
                flake8
                isort
              ]
              );
            }
          );
          packages.default = pkgs.stdenv.mkDerivation {
            pname = "flux-core";
            configureFlags = [ "--enable-content-s3" ];
            version = "${version_base}${version_suffix}";
            buildInputs = with pkgs ; [
              # hooks
              autoreconfHook

              libxcrypt # for libcrypt
              libsodium
              zeromq
              czmq
              jansson
              munge
              ncurses
              lz4
              sqlite
              libuuid
              hwloc
              libs3
              libevent
              libarchive
              libuv
              baseLua
              basePython

              # for completeness and tests
              mpi
              systemd
            ] ++ (with baseLua.pkgs; [
              luaposix
            ]) ++ (with basePython.pkgs; [
              cffi
              pyyaml
              jsonschema_3
              sphinx
              ply
            ]);
            nativeBuildInputs = with pkgs; [
              bash
              dash

              # build system
              pkg-config
              autoconf
              automake
              libtool
              m4

              # run tests
              jq
              libfaketime
              valgrind
            ];

            enableParallelBuilding = true;
            src = self;
            hardeningDisable = [ "bindnow" ];
            autoreconfPhase = ''
              export FLUX_VERSION=$version
              ./autogen.sh
            '';
            preBuild = ''
              # zsh can cause problems with buildPhase, use something fast
              export SHELL=dash
              patchShebangs src
              patchShebangs doc
              patchShebangs etc
            '';
          };
        }
      );
}
