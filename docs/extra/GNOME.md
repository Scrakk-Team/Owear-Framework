<!-- Copyright 2026 Owear Contributors
     SPDX-License-Identifier: Apache-2.0 -->

# Tray en Linux y escritorios modernos (GNOME)

Owear implementa el icono de bandeja en Linux con **dos backends**, elegidos en
runtime:

1. **AppIndicator (StatusNotifierItem)** — se carga con **`dlopen`** de
   `libayatana-appindicator3.so.1` (o `libappindicator3.so.1`). **No requiere
   headers de dev** para compilar. Es el estándar moderno y **funciona en
   GNOME/Zorin (con la extensión appindicator), KDE y otros**.
2. **`GtkStatusIcon`** (fallback) — bandeja X11 clásica (XEmbed): XFCE, MATE,
   Cinnamon, LXQt… si no está la lib de AppIndicator.

> No hace falta instalar nada para compilar. Si la librería existe en runtime
> (suele estar en Zorin/Ubuntu/KDE), se usa AppIndicator; si no, GtkStatusIcon.

## Matriz de escritorios

| Escritorio | Backend usado | ¿Se ve? |
|---|---|---|
| **Zorin / GNOME** (con extensión appindicator) | AppIndicator | ✅ (con la extensión, que Zorin trae) |
| GNOME **sin** extensión appindicator | AppIndicator (registra, pero el host no lo muestra) | ❌ → activa la extensión |
| KDE Plasma | AppIndicator | ✅ |
| XFCE / MATE / Cinnamon / LXQt | GtkStatusIcon (o AppIndicator) | ✅ |
| Wayland puro (Sway/Hyprland) | AppIndicator | ✅ con un host SNI (p.ej. `waybar`) |

## Comprobar qué se está usando
- Si la lib está: `ldconfig -p | grep -i appindicator`.
- La extensión en GNOME: `gnome-extensions list | grep -i appindicator`.

## Limitaciones (documentadas)
- Con **AppIndicator**, el icono **abre el menú** al hacer click; los eventos
  `click`/`right-click`/`double-click` **no** se emiten (limitación del estándar).
  Con **GtkStatusIcon** sí se emiten (`click`/`right-click`); el doble-click no es
  fiable.
- `setTitle` con AppIndicator usa una **etiqueta** junto al icono (según host);
  `setToolTip` se mapea al título del indicador.
- Si no hay bandeja/host, no hay nada que mostrar (no es un fallo de la app).
