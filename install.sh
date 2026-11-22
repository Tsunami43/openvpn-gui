#!/bin/sh
# Installs OpenVPN GUI on Arch Linux and Arch-based distributions:
#
#   curl -fsSL https://raw.githubusercontent.com/Tsunami43/openvpn-gui/main/install.sh | sh
#
# On x86_64 it trusts the project's package signing key, adds the signed
# pacman repository to /etc/pacman.conf and installs the package, so
# pacman -Syu keeps it up to date. Elsewhere it builds the latest release from
# source with makepkg. Running it again is harmless.
set -eu

REPO_URL=https://github.com/Tsunami43/openvpn-gui
KEY_ID=69B2A58A0FC6BAF7A541DAD2CD1A2C3155059EFD
PACMAN_CONF=${PACMAN_CONF:-/etc/pacman.conf}

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[1;31m==> %s\033[0m\n' "$*" >&2; exit 1; }

# Piped into sh, standard input is this script, so questions go to the
# terminal, or are skipped when there is none.
has_terminal() { (exec </dev/tty) 2>/dev/null; }

# No package for this machine: build the latest release with makepkg (its
# output goes to a log, shown only when the build fails) and install it.
build_from_source() {
  say "No package for $(uname -m) yet: building OpenVPN GUI from source"
  tag=$(curl -fsSL https://api.github.com/repos/Tsunami43/openvpn-gui/releases/latest \
    | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n1)
  [ -n "$tag" ] || fail "Could not find the latest release."
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  curl -fsSL "$REPO_URL/archive/refs/tags/$tag.tar.gz" | tar -xz -C "$work"
  src=$(echo "$work"/openvpn-gui-*)

  # What the package needs to build and run, from its PKGBUILD.
  needed=$(bash -c '. "$1"; echo base-devel "${depends[@]}" "${makedepends[@]}"' sh "$src/pkgbuild/PKGBUILD")
  # shellcheck disable=SC2086 # a list of package names
  missing=$(pacman -T $needed || true)
  if [ -n "$missing" ]; then
    say "Installing what it needs:" $missing
    # shellcheck disable=SC2086
    sudo pacman -S --needed --noconfirm --asdeps $missing >/dev/null
  fi

  say "Building OpenVPN GUI ${tag#v} (a minute or two)"
  log="$work/build.log"
  if ! (cd "$src/pkgbuild" && makepkg -f --nodeps --noconfirm) >"$log" 2>&1; then
    tail -n 25 "$log" >&2
    fail "The build failed; the lines above are the end of its log."
  fi

  say "Installing OpenVPN GUI ${tag#v}"
  sudo pacman -U --noconfirm "$src"/pkgbuild/openvpn-gui-*.pkg.tar.* >/dev/null
  say "Done. OpenVPN GUI is in the app launcher; run this command again to update it."
}

# All in a function called on the last line, so a download cut short runs
# nothing.
main() {
  if ! command -v pacman >/dev/null 2>&1; then
    echo "OpenVPN GUI needs Arch Linux (pacman was not found)." >&2
    exit 1
  fi

  if [ "$(uname -m)" != x86_64 ]; then
    build_from_source
    return
  fi

  say "Trusting the OpenVPN GUI package signing key"
  sudo pacman-key --add - >/dev/null <<'KEY'
-----BEGIN PGP PUBLIC KEY BLOCK-----

mDMEawNG7xYJKwYBBAHaRw8BAQdA9kd6gD0iCfMN83BpN9ycVYIksLYCr306C529
BjnKvG60Sk9wZW5WUE4gR1VJIHBhY2thZ2VzIChHaXRIdWIgQWN0aW9ucykgPFRz
dW5hbWk0M0B1c2Vycy5ub3JlcGx5LmdpdGh1Yi5jb20+iJAEExYKADgWIQRpsqWK
D8a696VB2tLNGiwxVQWe/QUCawNG7wIbAwULCQgHAgYVCgkICwIEFgIDAQIeAQIX
gAAKCRDNGiwxVQWe/bFQAP9WpE+YpUGU2Z3GM+ZOXYRCRyPRBPviQmVinh34Ep72
WwEAk9byINI0tWizKZrf9rfnVnCPzwxrQR+wCmvdNtJn4ww=
=T6no
-----END PGP PUBLIC KEY BLOCK-----
KEY
  sudo pacman-key --lsign-key "$KEY_ID" >/dev/null

  if grep -q '^\[openvpn-gui\]' "$PACMAN_CONF"; then
    say "The openvpn-gui repository is already in $PACMAN_CONF"
  else
    say "Adding the openvpn-gui repository to $PACMAN_CONF"
    printf '\n[openvpn-gui]\nServer = %s/releases/download/repo-$arch\n' "$REPO_URL" \
      | sudo tee -a "$PACMAN_CONF" >/dev/null
  fi

  say "Installing OpenVPN GUI"
  if has_terminal; then
    sudo pacman -Syu --needed openvpn-gui </dev/tty
  else
    sudo pacman -Syu --needed --noconfirm openvpn-gui
  fi
  say "Done. OpenVPN GUI is in the app launcher; pacman -Syu keeps it up to date."
}

main "$@"
