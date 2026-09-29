// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/win/Create.cpp — destructores + PCreate (Win32).
#include "Internal.hpp"
#include "PlatformData.hpp"
#include "../../../Core/Log.hpp"
#include "../../../Core/App.hpp"
#include "../../../Control/ControlServer.hpp"
#include "../../../Protocol/ProtocolRegistry.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <string>

namespace ow {

Window::~Window() = default;
Window::Impl::~Impl() {
    alive->store(false); // callbacks diferidos dejan de tocar this
    delete pdata;
}

bool Window::Impl::PCreate() {
    log::Info("window", "PCreate: inicio");
    pdata = new PlatformData();
    RegisterClassOnce();

    DWORD style = WS_OVERLAPPEDWINDOW;
    pdata->customTitlebar = opts.titleBarStyle == TitleBarStyle::Custom &&
                            !opts.frameless;
    if (opts.frameless || opts.titleBarStyle == TitleBarStyle::Hidden)
        style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
                (opts.resizable ? WS_MAXIMIZEBOX | WS_MINIMIZEBOX : 0);
    // Custom: conservamos WS_OVERLAPPEDWINDOW (Electron hace lo mismo: así DWM
    // mantiene esquinas redondeadas/sombra y las animaciones min/max en Win11).
    // El caption se oculta con WM_NCCALCSIZE + SWP_FRAMECHANGED (abajo), no
    // quitando WS_CAPTION.

    {
        char sbuf[16];
        std::snprintf(sbuf, sizeof(sbuf), "0x%08lX",
                      static_cast<unsigned long>(style));
        log::Info("window", std::string("PCreate: estilo=") + sbuf +
                                " custom=" + (pdata->customTitlebar ? "1" : "0"));
    }

    std::wstring title = Utf8ToWide(opts.title);
    HWND hwnd = CreateWindowExW(0, kOwWindowClass, title.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, opts.width, opts.height,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        log::Error("window", "CreateWindowExW falló: " +
                                 std::to_string(GetLastError()));
        return false;
    }
    log::Info("window", "PCreate: hwnd creado");

    pdata->hwnd = hwnd;
    HwndMap()[hwnd] = this;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pdata));
    // NO subclasar: la clase OwearWindow ya registra OwWndProc. Un
    // SetWindowLongPtr(GWLP_WNDPROC, OwWndProc) aquí devolvería como
    // origProc el propio OwWndProc → CallWindowProcW recursivo infinito
    // → 0xC00000FD en la init de WebView2 (mordido en CI).
    pdata->origProc = nullptr;

    if (pdata->customTitlebar) {
        // CRÍTICO: reaplica el marco AHORA que pdata está puesto. El primer
        // WM_NCCALCSIZE ocurre dentro de CreateWindowEx (pdata aún null → lo
        // maneja DefWindowProc y el caption se queda). Esto lo fuerza con
        // nuestro handler → desaparece el titlebar del sistema (Win10).
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    // C9: opciones de estilo/estado
    {
        LONG_PTR st = GetWindowLongPtrW(hwnd, GWL_STYLE);
        st = opts.resizable ? (st | WS_THICKFRAME) : (st & ~WS_THICKFRAME);
        st = opts.maximizable ? (st | WS_MAXIMIZEBOX) : (st & ~WS_MAXIMIZEBOX);
        st = opts.minimizable ? (st | WS_MINIMIZEBOX) : (st & ~WS_MINIMIZEBOX);
        if (!opts.closable) st &= ~WS_SYSMENU;
        SetWindowLongPtrW(hwnd, GWL_STYLE, st);
        if (opts.skipTaskbar) {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW;
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        }
        if (opts.alwaysOnTop)
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        pdata->aspect = opts.aspectRatio;
        if (opts.parent) {
            auto it = LiveWindows().find(opts.parent);
            if (it != LiveWindows().end()) {
                HWND owner = static_cast<HWND>(it->second->NativeHandle());
                if (owner) {
                    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT,
                                      reinterpret_cast<LONG_PTR>(owner));
                    if (opts.modal) EnableWindow(owner, FALSE);
                }
            }
        }
    }

    log::Info("window", "PCreate: webview->Create");
    std::vector<std::string> wargs = opts.webviewArgs;
    for (const auto& a : internal::CommandArgs()) wargs.push_back(a);
    if (!webview->Create(hwnd, WebviewArgsWithSession(wargs, opts.session))) {
        log::Error("window", "backend webview rechazó la creación");
        return false;
    }
    webview->SetEventSink([this](const std::string& name, std::string_view json) {
        Window::Impl::EmitPlatformEvent(this, name, json);
    });
    if (!opts.backgroundColor.empty()) PSetBackgroundColor(opts.backgroundColor);

    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    // protocol API: esquemas registrados por el main antes de crear la ventana.
    for (const auto& s : ProtocolRegistry::Get().All())
        webview->RegisterProtocol(s.name);

    // titleBarOverlay: botones nativos (min/max/close) sobre la titlebar custom.
    if (pdata->customTitlebar && opts.titleBarOverlay.enabled) {
        RegisterCaptionClassOnce();
        pdata->captionH =
            opts.titleBarOverlay.height > 0 ? opts.titleBarOverlay.height : 32;
        pdata->capBg = ParseHexColorRef(opts.titleBarOverlay.color, RGB(32, 32, 32));
        pdata->capFg =
            ParseHexColorRef(opts.titleBarOverlay.symbolColor, RGB(255, 255, 255));
        pdata->captionBar = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCaptionClass,
            L"", WS_POPUP, 0, 0, 10, 10, hwnd /*owner*/, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (pdata->captionBar) {
            SetWindowLongPtrW(pdata->captionBar, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(pdata));
            PositionCaptionBar(pdata);
            webview->InjectInitScript(
                "window.__owTitlebarOverlay={enabled:true,height:" +
                std::to_string(pdata->captionH) + ",width:" +
                std::to_string(pdata->capW) + ",top:0,right:0};");
            char cbuf[64];
            std::snprintf(cbuf, sizeof(cbuf), "bg=0x%06lX fg=0x%06lX",
                          static_cast<unsigned long>(pdata->capBg),
                          static_cast<unsigned long>(pdata->capFg));
            log::Info("window", std::string("titleBarOverlay: barra creada ") +
                                    cbuf + " w=" + std::to_string(pdata->capW) +
                                    " h=" + std::to_string(pdata->capH));
            SetTimer(hwnd, 1, 400, nullptr);
        }
    }

    if (opts.show) ShowWindow(hwnd, SW_SHOW);
    log::Info("window", "PCreate: ok");
    return true;
}

} // namespace ow
