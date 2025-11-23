{
  description = "A very basic flake for developing with the gnu toolchain";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
  let
    system = "x86_64-linux";
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    devShells.${system}.default = pkgs.mkShell {
      buildInputs = with pkgs; [
        gcc
        gnumake
        bear
        clang-tools
        fish
        dbus
        dbus.lib
      ];

      shellHook = ''
        exec fish
      '';
      NIX_CFLAGS_COMPILE=''-isystem ${pkgs.dbus.dev}/include/dbus-1.0 -isystem ${pkgs.dbus.lib}/lib/dbus-1.0/include'';
    };
  };
}
