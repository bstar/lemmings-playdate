{
  description = "Lemmings preservation and Playdate port environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          python = pkgs.python3.withPackages (ps: [ ps.pillow ]);
          # clang leads so the host and test builds match the macOS development
          # toolchain, whose warning set the -Werror sources are tuned to; the
          # Simulator target also invokes `clang` by name. gcc stays available.
          portTools = with pkgs; [
            clang
            gcc
            curl
            diffutils
            ffmpeg
            file
            git
            gnumake
            gnutar
            gzip
            nodejs_22
            python
            unzip
            which
            xz
            zip
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            gcc-arm-embedded
          ];
          # `make decompile` only. Kept out of the default shell so the common
          # build does not pull the Ghidra/JDK closure.
          decompileTools = with pkgs; [
            ghidra
            jdk21
            nasm
          ];
          # The prebuilt pdc/PlaydateSimulator binaries are linked against a
          # normal FHS layout; nix-ld plus this library path makes them run.
          playdateRuntime = with pkgs; [
            adwaita-icon-theme
            cairo
            gdk-pixbuf
            glib
            gtk3
            hicolor-icon-theme
            libpng
            librsvg
            libunwind
            libxkbcommon
            pango
            stdenv.cc.cc
            systemd
            webkitgtk_4_1
            libx11
            zlib
          ];
          shellHook = ''
            # Pin these rather than inheriting whichever cc-wrapper setup hook
            # ran last, so the host build is the same on every machine.
            export CC=clang
            export CXX=clang++
            export PYTHON=${python}/bin/python3
            export PLAYDATE_SDK_PATH="''${PLAYDATE_SDK_PATH:-$PWD/build/reference/playdate-sdk}"
            export PATH="$PLAYDATE_SDK_PATH/bin:$PATH"
            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath playdateRuntime}:''${LD_LIBRARY_PATH:-}"
              export GDK_PIXBUF_MODULE_FILE="${pkgs.librsvg}/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
            ''}
          '';
        in {
          default = pkgs.mkShell {
            packages = portTools;
            inherit shellHook;
          };
          decompile = pkgs.mkShell {
            packages = portTools ++ decompileTools;
            shellHook = shellHook + ''
              export JAVA_HOME=${pkgs.jdk21}
              export GHIDRA=${pkgs.ghidra}/bin/ghidra
              export GHIDRA_HEADLESS=${pkgs.ghidra}/lib/ghidra/support/analyzeHeadless
            '';
          };
        });
    };
}
