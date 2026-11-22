#!/bin/sh
# Installs OpenVPN GUI on Arch Linux / Omarchy:
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

# All in a function called on the last line, so a download cut short runs
# nothing.
main() {
if ! command -v pacman >/dev/null 2>&1; then
  echo "OpenVPN GUI needs Arch Linux (pacman was not found)." >&2
  exit 1
fi

if [ "$(uname -m)" != x86_64 ]; then
  say "No package for $(uname -m) yet: building the latest release from source"
  tag=$(git ls-remote --tags --refs --sort=-v:refname "$REPO_URL" 'v*' | head -n1 | sed 's|.*refs/tags/||')
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
  git clone --quiet --depth 1 --branch "$tag" "$REPO_URL.git" "$work/openvpn-gui"
  "$work/openvpn-gui/bin/install"
  exit 0
fi

say "Trusting the OpenVPN GUI package signing key"
sudo pacman-key --add - <<'KEY'
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
# Piped into sh, standard input is this script: pacman asks on the terminal,
# or goes ahead without asking when there is none.
if (exec </dev/tty) 2>/dev/null; then
  sudo pacman -Syu --needed openvpn-gui </dev/tty
else
  sudo pacman -Syu --needed --noconfirm openvpn-gui
fi
say "Done. OpenVPN GUI is in the app launcher; pacman -Syu keeps it up to date."
}

main "$@"
