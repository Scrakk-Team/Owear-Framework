<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Tray en Linux y escritorios modernos (GNOME)

Owear implementa el icono de bandeja en Linux con **StatusNotifierItem (SNI)**
nativo, hablando **directamente por D-Bus (GDBus)** — sin librerías externas.
Implementa a mano `org.kde.StatusNotifierItem` (icono, título, estado, métodos
`Activate`/`SecondaryActivate`/`ContextMenu`) y `com.canonical.dbusmenu` (el menú
contextual), y se registra con `org.kde.StatusNotifierWatcher`.

> Antes se intentaba `libayatana-appindicator` por `dlopen`, pero en algunos
> sistemas (p. ej. Zorin/GNOME) esa librería toma su *fallback* interno
> (`GtkStatusIcon`) y no registra SNI. El SNI nativo es determinista.

## Matriz de escritorios

| Escritorio | ¿Se ve? |
|---|---|
| **Zorin / GNOME** (con extensión appindicator) | ✅ (con la extensión, que Zorin trae) |
| GNOME **sin** extensión appindicator | ❌ → activa la extensión |
| KDE Plasma | ✅ |
| XFCE / MATE / Cinnamon / LXQt (con plugin SNI) | ✅ |
| Wayland puro (Sway/Hyprland) | ✅ con un host SNI (p.ej. `waybar`) |

## Comprobar el entorno
- Extensión en GNOME: `gnome-extensions list --enabled | grep -i appindicator`.
- ¿Hay watcher? `gdbus call --session --dest org.kde.StatusNotifierWatcher \
  --object-path /StatusNotifierWatcher \
  --method org.freedesktop.DBus.Properties.Get \
  org.kde.StatusNotifierWatcher RegisteredStatusNotifierItems`.

## Limitaciones (documentadas)
- El icono **abre el menú** (el host lo pide por `ContextMenu`/lee `Menu`); los
  eventos `click`/`right-click`/`double-click` **no** se emiten como en Windows
  (limitación del estándar SNI en Linux). Windows sí los emite.
- `setTitle` se usa como título del indicador; `setToolTip` se mapea igual.
- `setPressedImage` no aplica.
- Sin watcher/host (p. ej. GNOME sin la extensión) no hay nada que mostrar; no es
  un fallo de la app.
