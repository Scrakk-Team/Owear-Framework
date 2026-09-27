<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Tray en Linux y el caso GNOME

Owear implementa el icono de bandeja en Linux con **`GtkStatusIcon` (GTK3)**, sin
dependencias extra. Es el mecanismo **legacy** de bandeja, y su visibilidad
depende del escritorio:

| Escritorio | `GtkStatusIcon` (X11) | Notas |
|---|---|---|
| XFCE, KDE (X11), MATE, Cinnamon, LXQt | ✅ se ve | Hay "system tray" clásico |
| GNOME (X11) | ⚠️ normalmente **no** | GNOME quitó la bandeja clásica |
| GNOME (Wayland) | ❌ no | Igual que arriba |
| Sway/Hyprland (Wayland) | ❌ | Sin bandeja XEmbed |

## ¿Por qué no AppIndicator por defecto?
El estándar moderno en GNOME es **`libayatana-appindicator` (StatusNotifierItem)**,
pero:
1. Requiere `libayatana-appindicator3-dev` instalado (no viene por defecto).
2. En GNOME necesita además la **extensión "AppIndicator and KStatusNotifierItem
   Support"** (no viene de serie; en Ubuntu sí).

Por eso Owear usa `GtkStatusIcon` (compila siempre, funciona en la mayoría de
escritorios) y deja AppIndicator como mejora opcional futura.

## Qué hacer según el escritorio
- **XFCE/KDE/MATE/Cinnamon**: funciona tal cual.
- **GNOME**: 
  - Ubuntu: instala la extensión *AppIndicator* (suele venir) y, si acaso,
    `sudo apt install gnome-shell-extension-appindicator`.
  - O usa **X11** + la extensión *Tray Icons: Reloaded*.
- **Wayland puro (Sway/Hyprland)**: no hay soporte de bandeja; documenta en tu
  app que el icono puede no aparecer.

## Comportamiento en Owear
- Se soportan **click** (izquierdo), **right-click** (muestra el menú contextual)
  y el **menú contextual** (`setContextMenu`/`popupContextMenu`).
- **Doble-click** no es fiable con `GtkStatusIcon` (se emite solo donde el SO lo
  da); en Windows sí.
- `setTitle` es noop en Linux (GtkStatusIcon no tiene texto lateral).
- Si no hay bandeja, `GtkStatusIcon` puede emitir algún `Gtk-CRITICAL` interno
  (no es un fallo de la app; en un escritorio con bandeja no ocurre).
