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
      packages = with pkgs; [
        gcc
        gnumake
        bear
        clang-tools
        fish
        dbus.dev
        pkg-config
      ];

      shellHook = "exec fish";
    };
    packages.${system}.default = pkgs.stdenv.mkDerivation {
      name = "auto-power-profile";
      nativeBuildInputs = [ pkgs.pkg-config ];
      buildInputs = [ pkgs.dbus.dev ];
      makeFlags = [ "PREFIX=${placeholder "out"}"];
      src = ./.;
    };
  };
}
