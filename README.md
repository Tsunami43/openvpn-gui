# OpenVPN GUI

[![Build](https://github.com/Tsunami43/openvpn-gui/actions/workflows/build.yml/badge.svg)](https://github.com/Tsunami43/openvpn-gui/actions/workflows/build.yml)

![OpenVPN GUI connected to a saved profile](docs/screenshot.png)

OpenVPN GUI keeps your OpenVPN profiles and switches them on and off, from
its window or from its icon in the Omarchy bar.

Add a profile once: give it a name, pick the `.ovpn` file, and enter the
username and password if the server asks for them. After that, select it and
press Space. Omarchy's password dialog confirms the start, and the saved
login is answered for you.

## How it works

- **Profiles.** The `.ovpn` is copied to
  `~/.local/share/openvpn-gui/profiles/` (readable only by you), so moving or
  deleting the original later is fine. The list lives in
  `~/.config/openvpn-gui/profiles.json`. Files the profile refers to by a
  relative path still resolve beside the original.
- **Passwords** are kept in the system keyring (gnome-keyring, through
  libsecret), never in a file. If the server turns a saved password down, the
  app asks again and can remember the new one.
- **OpenVPN** runs as root, started once through `pkexec` with a management
  socket in `$XDG_RUNTIME_DIR/openvpn-gui`. The state, traffic, login and
  switching off all go over that socket. The package installs a polkit rule
  (`/usr/share/polkit-1/rules.d/49-openvpn-gui.rules`) that lets members of
  `wheel` at an active local session start `/usr/bin/openvpn` without a
  password; everyone else is asked for it when switching on. Note that this
  lets those users run OpenVPN as root with any profile, which can run
  scripts, so it is about as strong as passwordless sudo. OpenVPN's log is in `$XDG_RUNTIME_DIR/openvpn-gui/openvpn.log`.
- **Updates.** At start and every six hours the app looks at the latest
  release on GitHub. When it is newer, an **Update to …** button appears at
  the top: it downloads the release's package for this machine, installs it
  with `pkexec pacman -U` (asking for your password) and restarts the app. A
  release without a package for this machine opens its page instead. To
  release, bump `VERSION` in `openvpn-gui.pro` and `pkgver` in the PKGBUILD,
  then push a `v…` tag. GitHub Actions attaches the x86_64 package to the
  release and, when the `AUR_ENABLED` repository variable is `true`, publishes
  `aur/PKGBUILD` with the new version to the AUR, using the
  `AUR_SSH_PRIVATE_KEY` repository secret.
- **The bar.** Closing the window keeps the app in the Omarchy bar. Click
  the icon to bring the window back; its menu connects a profile,
  disconnects, or quits. Quitting switches the tunnel off. Launching the app
  again shows the running window instead of starting a second copy.

## Shortcuts

| Key    | Action                       |
|--------|------------------------------|
| Space  | Connect or disconnect        |
| ↑ ↓    | Choose a profile             |
| N      | New profile                  |
| E      | Edit the profile             |
| Delete | Delete the profile           |
| Esc    | Hide to the bar              |
| ?      | Show shortcuts               |
| Q      | Quit (disconnects)           |

## Requirements

Arch Linux (Omarchy) with:

- Qt 6: `qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-wayland`
- `openvpn`, `polkit`, `libsecret` (with gnome-keyring running)
- `xdg-desktop-portal` for the file picker
- To build: `gcc`, `make`, `pkgconf`

## Install

On x86_64, from the latest release (the app then offers its own updates):

```sh
sudo pacman -U https://github.com/Tsunami43/openvpn-gui/releases/download/v0.3.0/openvpn-gui-0.3.0-1-x86_64.pkg.tar.zst
```

Or from source:

```sh
git clone https://github.com/Tsunami43/openvpn-gui.git
cd openvpn-gui
bin/install
```

This builds the app, packages it with `makepkg` and installs it, so OpenVPN
GUI appears in the app launcher.

## Build

```sh
bin/build      # build/openvpn-gui
bin/test       # Qt Test, offscreen
bin/install    # makepkg -fsi, adds OpenVPN GUI to the app launcher
```

`build/openvpn-gui path/to/profile.ovpn` opens the new-profile dialog with
that file.
