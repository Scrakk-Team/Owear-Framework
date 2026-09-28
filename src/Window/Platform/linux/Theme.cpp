// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Theme.cpp — color/tema (helpers puros de Window).
#include "Internal.hpp"

#include <gtk/gtk.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace ow {

/// "#RGB"/"#RRGGBB"/"#RRGGBBAA" → "#rrggbb" (o "" si transparente/inválido).
std::string CssHex(const std::string& in) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() == 3) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 && s.size() != 8) return {};
    if (s.size() == 8 && std::strtoul(s.substr(6, 2).c_str(), nullptr, 16) == 0)
        return {}; // alpha 0 ⇒ dejar transparente
    return "#" + s.substr(0, 6);
}

/// "#rrggbb" → "#000000"/"#ffffff" según luminancia (contraste legible).
std::string ContrastHex(const std::string& hex) {
    if (hex.size() != 7) return "#ffffff";
    auto hx = [&](int i) {
        return std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16) / 255.0;
    };
    const double lum = 0.2126 * hx(1) + 0.7152 * hx(3) + 0.0722 * hx(5);
    return lum > 0.5 ? "#000000" : "#ffffff";
}

/// Aclara (amt>0) u oscurece (amt<0) un "#rrggbb".
std::string ShadeHex(const std::string& hex, double amt) {
    if (hex.size() != 7) return hex;
    auto hx = [&](int i) {
        return static_cast<int>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
    };
    auto adj = [amt](int v) {
        double x = v / 255.0;
        x = amt >= 0 ? x + (1.0 - x) * amt : x * (1.0 + amt);
        int r = static_cast<int>(x * 255.0 + 0.5);
        return r < 0 ? 0 : (r > 255 ? 255 : r);
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", adj(hx(1)), adj(hx(3)),
                  adj(hx(5)));
    return buf;
}

// ── radio de esquina del tema (para el redondeo del contenido web) ──────────
std::string TrimWs(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
}

/// Busca `decoration { ... border-radius: Npx ... }` en un CSS del tema.
int ParseDecorationRadius(const std::filesystem::path& p) {
    std::ifstream f(p);
    if (!f) return -1;
    std::string css((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    // Quita comentarios /* ... */ (si no, se cuelan en el selector).
    for (size_t c = css.find("/*"); c != std::string::npos; c = css.find("/*")) {
        const size_t end = css.find("*/", c + 2);
        css.erase(c, (end == std::string::npos ? css.size() : end + 2) - c);
    }
    size_t i = 0;
    while ((i = css.find('{', i)) != std::string::npos) {
        size_t start = 0;
        if (size_t c = css.rfind('}', i); c != std::string::npos) start = c + 1;
        for (char sep : {';', '@'}) {
            if (size_t c = css.rfind(sep, i); c != std::string::npos && c >= start)
                start = c + 1;
        }
        const std::string sel = TrimWs(css.substr(start, i - start));
        const size_t e = css.find('}', i);
        if (e == std::string::npos) break;
        if (sel == "decoration" || sel == "decoration:backdrop") {
            const std::string body = css.substr(i + 1, e - i - 1);
            if (size_t br = body.find("border-radius"); br != std::string::npos) {
                if (size_t colon = body.find(':', br); colon != std::string::npos) {
                    if (size_t d = body.find_first_of("0123456789", colon);
                        d != std::string::npos)
                        return std::atoi(body.c_str() + d);
                }
            }
        }
        i = e + 1;
    }
    return -1;
}

/// Radio de esquina de la ventana según el tema activo (fallback 10).
int ThemeWindowRadius() {
    GtkSettings* s = gtk_settings_get_default();
    gchar* name = nullptr;
    g_object_get(s, "gtk-theme-name", &name, nullptr);
    const std::string theme = name ? name : "";
    g_free(name);
    if (theme.empty()) return 10;

    std::vector<std::filesystem::path> roots;
    if (const char* home = std::getenv("HOME")) {
        roots.push_back(std::filesystem::path(home) / ".themes");
        roots.push_back(std::filesystem::path(home) / ".local/share/themes");
    }
    const char* xdg = std::getenv("XDG_DATA_DIRS");
    std::string dirs = (xdg && *xdg) ? xdg : "/usr/local/share:/usr/share";
    for (size_t a = 0, b = 0; a <= dirs.size(); a = b + 1) {
        b = dirs.find(':', a);
        if (b == std::string::npos) b = dirs.size();
        if (b > a) roots.emplace_back(dirs.substr(a, b - a) + "/themes");
    }

    for (const auto& r : roots) {
        for (const char* css : {"gtk.css", "gtk-dark.css"}) {
            const int rad = ParseDecorationRadius(r / theme / "gtk-3.0" / css);
            if (rad > 0) return rad;
        }
    }
    return 10;
}


} // namespace ow
