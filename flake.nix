{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      python = pkgs.python3.withPackages (ps: with ps; [
        pyroute2
      ]);
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell {
        packages = [ python ];
        shellHook = ''
          export PYTHONPATH="$PWD/src:$PYTHONPATH"
        '';
      };
    };
}
