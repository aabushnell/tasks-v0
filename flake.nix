{
  description = "tasks-v0 dev shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          inherit (pkgs.lib) optionals optionalString;
          inherit (pkgs.stdenv.hostPlatform) isLinux isDarwin;
        in
        {
          default = pkgs.mkShell {
            packages =
              with pkgs;
              [
                # c
                gcc
                clang-tools
                bear
              ]
              ++ optionals isLinux [
                # void
              ]
              ++ optionals isDarwin [
                # void
              ];

            shellHook = ''
            '';
          };
        }
      );
    };
}


