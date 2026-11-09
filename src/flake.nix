{
  description = "stencil environment for demo-runtime";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system}.stencil-env = pkgs.stdenv.mkDerivation {
        pname = "stencil-env";
        version = "0.1.0";
        src = ./.;

        buildPhase = ''
          $CXX -std=c++20 -O2 -shared -fPIC -o env.so env_stencil.cpp
        '';
        installPhase = ''
          mkdir -p $out/lib
          cp env.so $out/lib/env.so
        '';
      };
    };
}

