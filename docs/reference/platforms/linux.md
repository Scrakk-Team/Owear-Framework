---
title: Linux
description: Linux is the reference platform  the kernel, stock modules, and E2E suites are
order: 1
---
<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Linux

Linux is the reference platform: the kernel, stock modules, and E2E suites are
verified here end to end.

## Backend

- Windowing and UI: **GTK 3**.
- WebView: **WebKitGTK 4.1** (libsoup 3).
- Clipboard, dialogs, screen, menu, notifications, power, and shell use GTK/GIO
  and D-Bus.

## Building from source

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev \
  libssl-dev zlib1g-dev ninja-build xvfb libayatana-appindicator3-dev
cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
```

Required CMake presets: `linux-debug`, `linux-release`.

## Desktop integration notes

- **Application menu (`Menu.setApplicationMenu`)** is a **no-op**. GNOME has no
  global menubar, so apps draw their own in web code. Menu accelerators still work.
- **Tray** is implemented with native **StatusNotifierItem** + dbusmenu over
  GDBus (no libappindicator). It needs a StatusNotifierWatcher:

  | Desktop | Tray visible? |
  |---|---|
  | Zorin / GNOME + appindicator extension | yes |
  | GNOME without the extension | no (enable the extension) |
  | KDE Plasma | yes |
  | XFCE / MATE / Cinnamon / LXQt + SNI plugin | yes |
  | Pure Wayland (Sway/Hyprland) with an SNI host (e.g. `waybar`) | yes |

  Check the extension: `gnome-extensions list --enabled | grep -i appindicator`.
  Check for a watcher:
  `gdbus call --session --dest org.kde.StatusNotifierWatcher --object-path /StatusNotifierWatcher --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems`.

  On Linux the tray emits `click`/`right-click` but the SNI standard has the host
  open the menu, so popup-menu events may not fire the way they do on Windows.
  `setPressedImage` does not apply.

- **Global shortcuts** and **screen capture** are **X11-only**. Under Wayland they
  return clear errors (`globalShortcut requires an X11 session`, `capture is only
  supported on an X11 session`) because compositors isolate these capabilities.
- **Idle time** uses X11 MIT-SCREEN-SAVER (`libXss`, `dlopen`ed); on Wayland it
  reports `unknown`/`0`.

## Headless testing

E2E suites need a display; CI uses Xvfb:

```bash
MODS=""
for d in build/linux-release/api/*/; do
  [ "$d" != "build/linux-release/api/CMakeFiles/" ] && MODS="$MODS$d:"
done
xvfb-run -a --server-args="-screen 0 1280x800x24" \
  env OW_APP_NAME=CI GDK_BACKEND=x11 OW_MODULES_DIR="${MODS%:}" \
  setsid ./build/linux-release/src/owear > /tmp/owear-e2e.log 2>&1 &
sleep 5
python3 tests/e2e/run_suites.py --pages all.html builtins.html veto.html --port 8123
```

Note `GDK_BACKEND=x11`: forcing X11 avoids the Wayland limitations above.

## Optional dependencies

`capturer`, `globalshortcut`, and `tray` are marked optional. If their system
dependencies are missing at build time, they are omitted from the build rather
than failing it.

## Known limitations

- `printToPDF` is unavailable in WebKitGTK v2.52 and returns a clear error.
- `findInPage` is degraded under Xvfb/software rendering
  (`VERIFY ON REAL DESKTOP`).
- Wayland screen capture (xdg-desktop-portal) is not implemented in v1.
