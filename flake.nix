{
  description = "Introduction to C";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { self
    , nixpkgs
    , utils
    ,
    }:
    utils.lib.eachDefaultSystem (system:
    let
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
    in
    {
      packages = rec {
        atlaswm = pkgs.stdenv.mkDerivation {
          name = "atlaswm";
          version = "1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            pkg-config
            gcc
            xorg.libX11
            xorg.libXinerama
            xorg.xorgproto
            xorg.libXft
            xorg.xinit
          ];

          buildPhase = ''
            make atlaswm
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp atlaswm $out/bin/
          '';
        };
        default = atlaswm;
      };

      devShells.default = pkgs.mkShell {
        buildInputs = with pkgs; [
          gcc
          pkg-config
          xorg.libX11
          xorg.xorgproto
          xorg.libXinerama
          xorg.libXft
          xorg.xinit
          clang
        ];
      };
    });
}
