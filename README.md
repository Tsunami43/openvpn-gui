# OpenVPN GUI

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
  switching off all go over that socket, so only switching on asks for your
  password. OpenVPN's log is in `$XDG_RUNTIME_DIR/openvpn-gui/openvpn.log`.
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
