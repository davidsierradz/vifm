{
  description = "vifm built from this checkout (with jump labels)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        # Reuse nixpkgs' recipe (dependencies, helpztags shebang patching), but
        # build from the local tree instead of a release tarball.
        vifm = pkgs.vifm.overrideAttrs (old: {
          version = "0.15-jump-${self.shortRev or self.dirtyShortRev or "dirty"}";
          src = self;
          # Keep the binary reproducible (no build date in `vifm --version`).
          configureFlags = (old.configureFlags or [ ]) ++ [ "--disable-build-timestamp" ];
          doCheck = false;
        });
        default = vifm;
      });

      apps = forAllSystems (pkgs: rec {
        vifm = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.vifm}/bin/vifm";
        };
        default = vifm;
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.vifm ];
          packages = [
            pkgs.gnumake
            pkgs.gdb
          ];
        };
      });
    };
}
