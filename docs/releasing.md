# Releasing

1. Bump `VERSION` in `openvpn-gui.pro` and `pkgver` in `pkgbuild/PKGBUILD`
   (reset `pkgrel` to 1), commit, and push a `v…` tag:

   ```sh
   git tag -a v0.5.0 -m "OpenVPN GUI 0.5.0"
   git push origin main v0.5.0
   ```

2. GitHub Actions (`.github/workflows/build.yml`) then:
   - runs the tests and builds the x86_64 package;
   - creates the release and attaches the package;
   - signs the package into the pacman repository, the `repo-x86_64`
     pre-release, with `bin/repo` and the `PACMAN_GPG_PRIVATE_KEY` secret.

A tag that already exists can be published again with
`gh workflow run build.yml -f tag=v0.5.0`.

## Keys

- **Package signing key** `69B2A58A0FC6BAF7A541DAD2CD1A2C3155059EFD`: the
  private half is only in the `PACMAN_GPG_PRIVATE_KEY` secret. The public
  half is in `pkgbuild/openvpn-gui.asc` and embedded in `install.sh`; a new
  key has to be put in both, and users have to run the install command again.
