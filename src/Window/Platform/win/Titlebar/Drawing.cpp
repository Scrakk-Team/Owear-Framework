// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Titlebar.cpp — titleBarOverlay (botones nativos Win).
#include "../Internal.hpp"
#include "../PlatformData.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <string>


namespace ow {

COLORREF BlendColor(COLORREF a, COLORREF b, double t) {
    int r = static_cast<int>(GetRValue(a) * (1 - t) + GetRValue(b) * t);
    int g = static_cast<int>(GetGValue(a) * (1 - t) + GetGValue(b) * t);
    int bl = static_cast<int>(GetBValue(a) * (1 - t) + GetBValue(b) * t);
    return RGB(r, g, bl);
}

COLORREF ParseHexColorRef(const std::string& in, COLORREF def) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() < 6) return def;
    auto hx = [](const std::string& x) {
        return static_cast<unsigned>(std::strtoul(x.c_str(), nullptr, 16));
    };
    return RGB(hx(s.substr(0, 2)), hx(s.substr(2, 2)), hx(s.substr(4, 2)));
}

/// Barra de botones (ventana hija OPACA) estilo Win10/11, como Electron:
/// minimizar = línea, maximizar = cuadrado, restaurar = dos cuadrados, cerrar =
/// X; icono 10px; hover blanco 10% (close #E81123). Fondo = titleBarOverlay.color.
void EnsureGdiplus() {
    static ULONG_PTR token = 0;
    if (!token) {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    }
}

/// Barra de botones (popup top-level LAYERED) dibujada con GDI+ (constantes de
/// Electron: icono 10px, min/max/restore sin AA y rect insetado 0.5, restore =
/// dos cuadrados de 8px desplazados 2, cerrar = X con AA recortada).
///
/// CLAVE del hit-test: en un layered con UpdateLayeredWindow Windows pasa el
/// mouse por los píxeles con alpha == 0. Para que el hover/press funcionen en
/// TODO el rect (y no solo sobre el glifo), el fondo se pinta con alpha = 1
/// (visualmente imperceptible) en vez de 0.
void DrawCaptionBar(HWND hwnd, Window::Impl::PlatformData* pd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0 || !pd) return;
    EnsureGdiplus();

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    UINT dpi = pd->hwnd ? GetDpiForWindow(pd->hwnd) : 96;
    if (dpi < 96) dpi = 96;
    const bool maximized = IsZoomed(pd->hwnd);
    const int bw = w / 3;
    int icon = MulDiv(10, static_cast<int>(dpi), 96);
    if (icon < 6) icon = 10;
    float stroke = static_cast<float>(MulDiv(1, static_cast<int>(dpi), 96));
    if (stroke < 1.0f) stroke = 1.0f;
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Gdiplus::Color(1, 0, 0, 0)); // alpha 1 = transparente pero hittable

        for (int i = 0; i < 3; ++i) {
            const int x0 = i * bw, x1 = (i == 2) ? w : (i + 1) * bw;
            const bool hot = pd->capHover == i;
            const bool press = pd->capPress == i;
            if (hot || press) {
                BYTE a;
                Gdiplus::Color base;
                if (i == 2) {
                    base = Gdiplus::Color(255, 0xE8, 0x11, 0x23);
                    a = press ? 0x98 : 255;
                } else {
                    base = Gdiplus::Color(255, 255, 255, 255);
                    a = press ? 0x33 : 0x1A;
                }
                Gdiplus::SolidBrush br(
                    Gdiplus::Color(a, base.GetR(), base.GetG(), base.GetB()));
                g.FillRectangle(&br, Gdiplus::Rect(x0, 0, x1 - x0, h));
            }
            Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), stroke);
            // Centrado en ENTEROS (Electron: ClampToCenteredSize). Con coords
            // float a veces caía en medio píxel y el AA de la X la difuminaba.
            const int iw = x1 - x0;
            const float S = static_cast<float>(icon);
            const float sx = static_cast<float>(x0 + (iw - icon) / 2);
            const float sy = static_cast<float>((h - icon) / 2);
            Gdiplus::RectF symbol(sx, sy, S, S);
            auto strokeRect = [&](const Gdiplus::RectF& r) {
                Gdiplus::RectF rr(r);
                rr.Inflate(-stroke * 0.5f, -stroke * 0.5f); // inset 0.5 (Electron)
                g.DrawRectangle(&pen, rr);
            };
            if (i == 0) { // minimizar: línea
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                g.DrawLine(&pen, sx, sy + S / 2.0f, sx + S, sy + S / 2.0f);
            } else if (i == 1 && !maximized) { // maximizar: cuadrado
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                strokeRect(symbol);
            } else if (i == 1) { // restaurar: dos cuadrados
                g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
                int sepI = static_cast<int>(2.0 * static_cast<double>(dpi) / 96.0);
                if (sepI < 1) sepI = 2;
                const float sep = static_cast<float>(sepI);
                Gdiplus::RectF front(sx, sy + sep, S - sep, S - sep);
                strokeRect(front);
                g.SetClip(front, Gdiplus::CombineModeExclude);
                strokeRect(Gdiplus::RectF(sx + sep, sy, S - sep, S - sep));
                g.ResetClip();
            } else { // cerrar: X (AA, recortada)
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetClip(symbol);
                g.DrawLine(&pen, sx, sy, sx + S, sy + S);
                g.DrawLine(&pen, sx + S, sy, sx, sy + S);
                g.ResetClip();
            }
        }
    }

    RECT wr;
    GetWindowRect(hwnd, &wr);
    POINT dst{wr.left, wr.top}, src{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}


} // namespace ow
