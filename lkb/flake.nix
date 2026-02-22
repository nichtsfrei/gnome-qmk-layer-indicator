{
  description = "Minimal Layered Keyboard Daemon";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable-small";
  };

  outputs =
    { self
    , nixpkgs
    }:
    let
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });

      version = "0.0.3";
      pname = "lkb";
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          ${pname} = pkgs.stdenv.mkDerivation {
            inherit pname version;
            src = ./.;

            nativeBuildInputs = [
              pkgs.pkg-config
            ];

            buildInputs = [
              pkgs.dbus
            ];

            buildPhase = ''
              pkg-config --cflags --libs dbus-1 > dbus_flags
              gcc -O3 -Wall daemon.c -o lkbd $(cat dbus_flags)
            '';

            installPhase = ''
              mkdir -p $out/bin
              install -m755 lkbd $out/bin/lkbd
              install -m755 lkb  $out/bin/lkb
            '';

            meta = with pkgs.lib; {
              description = "Layered Keyboard Daemon with DBus signaling";
              platforms = supportedSystems;
            };
          };
        });

      devShell = forAllSystems (system:
        let
          pkgs = nixpkgsFor.${system};
        in
        pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.pkg-config
            pkgs.bear
          ];

          buildInputs = [
            pkgs.gcc
            pkgs.clang-tools
            pkgs.netcat
            pkgs.dbus
          ];

          shellHook = ''
            rm -f compile_commands.json
            bear --append -- gcc -c daemon.c $(pkg-config --cflags --libs dbus-1)
            rm daemon.o
          '';
        });
    };
}
