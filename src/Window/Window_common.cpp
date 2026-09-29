// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Window_common.cpp — lógica común de Window (3 plataformas).
//
// F3.2: outbox — todos los _apply/_event se encolan y salen en UN solo
//       eval por tick del main loop (crítico durante resize storms).
// F3.4: BeginCloseFlow — veto nativo → aviso JS+SDK con requestId →
//       ventana de OW_CLOSE_TIMEOUT_MS (default 1000 ms) para responder →
//       destroy.
//
#include "Window_p.hpp"
#include "Common/Internal.hpp"
#include "../Bridge/Dispatcher.hpp"
#include "../Core/App.hpp"
#include "../Control/ControlServer.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"
#include "ow/Shm.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace ow {

using namespace window_detail;

// ── ciclo de vida ────────────────────────────────────────────────────────────

Window::Impl::Impl(Window* self, const WindowOptions& opts)
    : self(self), opts(opts) {}

void Window::Impl::InitCommon() {
    webview = CreateWebviewBackend();
    if (!webview) {
        log::Error("window", "no se pudo crear el backend de webview");
        return;
    }

    // PCreate crea la ventana nativa Y llama webview->Create(parent).
    if (!PCreate()) {
        log::Error("window", "PCreate falló");
        return;
    }
    log::StartupMark("ventana+webview creados");

    // MOSTRAR YA: el usuario ve la ventana sin esperar a WebKit (load_uri
    // bloquea ~200 ms y el bring-up del WebProcess ~800 ms). El contenido llega
    // después; la ventana ya está en pantalla.
    if (opts.show) PShow();
    log::StartupMark("ventana visible");

    // Scripts de inicio: corren en orden, ANTES de cualquier script de página.
    webview->InjectInitScript(BuildBridgeScript());
    webview->InjectInitScript("window.__owWindowId=" + std::to_string(id) + ";");
    log::StartupMark("scripts inyectados");

    webview->SetMessageHandler([this](std::string_view text) {
        HandleWebViewMessage(text);
    });
    log::StartupMark("handler listo");

    if (!opts.url.empty()) webview->LoadURL(opts.url);
    log::StartupMark("loadURL lanzado");
}

// ── delegación pública común ─────────────────────────────────────────────────

Window::Window(const WindowOptions& options) : impl_(new Impl(this, options)) {
    static std::atomic<uint32_t> s_next{1};
    impl_->id = s_next.fetch_add(1);
    impl_->InitCommon();
    // Toda ventana se registra + cablea aquí: así las creadas por el kernel
    // (p. ej. OW_DEMO) también reciben eventos de módulos.
    ControlServer::RegisterWindow(this);
}

WindowId Window::Id() const { return impl_->id; }
void Window::Show() { impl_->PShow(); }
void Window::Hide() { impl_->PHide(); }
void Window::Focus() { impl_->PFocus(); }
void Window::Minimize() { impl_->PMinimize(); }
void Window::Maximize() { impl_->PMaximize(); }
void Window::Unmaximize() { impl_->PUnmaximize(); }
void Window::Restore() { impl_->PRestore(); }
void Window::SetFullScreen(bool e) { impl_->PSetFullScreen(e); }
bool Window::IsMaximized() const { return impl_->PIsMaximized(); }
bool Window::IsMinimized() const { return impl_->PIsMinimized(); }
bool Window::IsFullScreen() const { return impl_->PIsFullScreen(); }
bool Window::IsVisible() const { return impl_->PIsVisible(); }
bool Window::IsFocused() const { return impl_->PIsFocused(); }
bool Window::IsResizable() const { return impl_->PIsResizable(); }
bool Window::IsMovable() const { return impl_->PIsMovable(); }
bool Window::IsMinimizable() const { return impl_->PIsMinimizable(); }
bool Window::IsMaximizable() const { return impl_->PIsMaximizable(); }
bool Window::IsClosable() const { return impl_->PIsClosable(); }
bool Window::IsAlwaysOnTop() const { return impl_->PIsAlwaysOnTop(); }
bool Window::IsKiosk() const { return impl_->PIsKiosk(); }
bool Window::IsDestroyed() const { return impl_->PIsDestroyed(); }
void Window::SetResizable(bool on) { impl_->PSetResizable(on); }
void Window::SetMovable(bool on) { impl_->PSetMovable(on); }
void Window::SetMinimizable(bool on) { impl_->PSetMinimizable(on); }
void Window::SetMaximizable(bool on) { impl_->PSetMaximizable(on); }
void Window::SetClosable(bool on) { impl_->PSetClosable(on); }
void Window::SetAlwaysOnTop(bool on, int level) { impl_->PSetAlwaysOnTop(on, level); }
void Window::SetSkipTaskbar(bool on) { impl_->PSetSkipTaskbar(on); }
void Window::SetHasShadow(bool on) { impl_->PSetHasShadow(on); }
void Window::SetKiosk(bool on) { impl_->PSetKiosk(on); }
void Window::SetIgnoreMouseEvents(bool ignore, bool forward) {
    impl_->PSetIgnoreMouseEvents(ignore, forward);
}
void Window::SetProgressBar(double value, const std::string& mode) {
    impl_->PSetProgressBar(value, mode);
}
void Window::SetBackgroundColor(const std::string& color) {
    impl_->PSetBackgroundColor(color);
}
void Window::MoveTop() { impl_->PMoveTop(); }
void Window::SetAspectRatio(double ratio, int extraW, int extraH) {
    impl_->PSetAspectRatio(ratio, extraW, extraH);
}
Window::Bounds Window::GetContentBounds() const { return impl_->PGetContentBounds(); }
void Window::SetContentSize(int w, int h) { impl_->PSetContentSize(w, h); }
Window::Size Window::GetContentSize() const { return impl_->PGetContentSize(); }
Window::Size Window::GetMinimumSize() const { return impl_->PGetMinimumSize(); }
Window::Size Window::GetMaximumSize() const { return impl_->PGetMaximumSize(); }
void Window::SetMinimumSize(int w, int h) { impl_->PSetMinimumSize(w, h); }
void Window::SetMaximumSize(int w, int h) { impl_->PSetMaximumSize(w, h); }
Window::Bounds Window::GetBounds() const { return impl_->PGetBounds(); }
void Window::SetBounds(const Bounds& b) { impl_->PSetBounds(b); }
void Window::Center() { impl_->PCenter(); }
void Window::SetTitle(const std::string& t) { impl_->PSetTitle(t); }
std::string Window::Title() const { return impl_->PGetTitle(); }
void Window::SetTitleBarStyle(TitleBarStyle s) {
    impl_->opts.titleBarStyle = s;
    impl_->PApplyTitleBar();
}
void Window::SetTitleBarOverlay(const TitleBarOverlay& o) {
    impl_->opts.titleBarOverlay = o;
    impl_->PApplyTitleBar();
}

std::string Window::CreateWebview(const std::string& optionsJson) {
    return impl_->PCreateWebview(optionsJson);
}
std::string Window::WebviewCommand(uint32_t id, const std::string& op,
                                   const std::string& argsJson) {
    return impl_->PWebviewCommand(id, op, argsJson);
}
void Window::RegisterProtocol(const std::string& scheme) {
    if (impl_->webview) impl_->webview->RegisterProtocol(scheme);
}

void Window::CapturePage(std::function<void(bool ok, const std::string& png)> cb) {
    if (impl_->webview) impl_->webview->CapturePage(std::move(cb));
    else if (cb) cb(false, {});
}

void Window::SetApplicationMenu(const std::string& itemsJson) {
    impl_->PSetApplicationMenu(itemsJson);
}

void Window::SetColorScheme(int mode) { impl_->PSetColorScheme(mode); }

void Window::PrintToPDF(std::function<void(bool ok, const std::string& pdf)> cb) {
    impl_->PPrintToPDF(std::move(cb));
}
void* Window::NativeHandle() const {
    return impl_->webview ? impl_->webview->NativeWidget() : nullptr;
}

void Window::Close() { impl_->BeginCloseFlow(); }
void Window::Destroy() { impl_->PDestroy(); }

void Window::LoadURL(const std::string& url) {
    impl_->opts.url = url;
    impl_->webview->LoadURL(url);
}

void Window::EvalJS(const std::string& js,
                    std::function<void(std::string_view)> callback) {
    impl_->webview->EvalJS(WrapEval(js),
                           [callback](std::string_view result, bool ok) {
                               if (callback) callback(ok ? result : std::string_view("null"));
                           });
}

} // namespace ow
