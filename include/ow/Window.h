// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Window.h — ventanas multiplataforma con titlebar custom/overlay.
// Contrato compartido: window_win.cpp / window_mac.mm / window_linux.cpp
//

#include "ow/Common.h"

namespace ow {

enum class TitleBarStyle {
    Default,   // decoraciones del sistema tal cual
    Hidden,    // sin titlebar; la ventana completa es contenido (frameless)
    Custom     // titlebar custom + botones nativos overlay (donde el SO lo permite)
};

struct TitleBarOverlay {
    bool enabled = false;
    std::string color = "#00000000";  // fondo de la banda (alpha 0 = transparente)
    // Color de los glifos. Vacío = lo decide el tema/SO.
    std::string symbolColor;
    // Color de fondo interno de los botones (el "círculo"). Vacío = tema/SO.
    std::string buttonColor;
    int height = 32;  // alto de la banda en px lógicos (default de Electron)
};

struct WindowOptions {
    std::string title = "Owear";
    int width = 1024;
    int height = 768;
    int minWidth = 0, minHeight = 0;
    int maxWidth = 0, maxHeight = 0;
    bool resizable = true;
    bool center = true;
    bool show = true;
    bool frameless = false;
    bool movable = true;
    bool minimizable = true;
    bool maximizable = true;
    bool closable = true;
    bool fullscreenable = true;
    bool skipTaskbar = false;
    bool alwaysOnTop = false;
    bool hasShadow = true;
    bool transparent = false;
    std::string backgroundColor;  // "#RRGGBB" o vacío = por defecto

    /// Ventana padre (owner/transient) y si es modal respecto a ella.
    WindowId parent = 0;
    bool modal = false;

    /// Relación de aspecto fija (0 = libre). `aspectExtraW/H` = margen extra.
    double aspectRatio = 0.0;
    int aspectExtraW = 0, aspectExtraH = 0;

    TitleBarStyle titleBarStyle = TitleBarStyle::Default;
    TitleBarOverlay titleBarOverlay;

    /// URL inicial: http(s):// (dev server), app://<bundle>/... o file://
    std::string url = "app://index.html";

    /// Partición de sesión (aislamiento de cookies/storage por perfil).
    /// Vacío = perfil por defecto de la app. Ej: "persist:cuenta-2".
    std::string session;

    /// Argumentos extra para el proceso del WebView (debug flags, etc).
    std::vector<std::string> webviewArgs;
};

/// Payload de evento como JSON serializado ("null" si el evento no lleva datos).
using EventPayload = std::string_view;
using ListenerId = uint64_t;

class Window : NonCopyable {
public:
    struct Bounds {
        int x = 0, y = 0, w = 0, h = 0;
    };
    struct Size {
        int width = 0, height = 0;
    };

    explicit Window(const WindowOptions& options);
    ~Window();

    WindowId Id() const;

    // ── ciclo de vida ──────────────────────────────────────────────
    void Show();
    void Hide();
    void Close();          // dispara closeRequested; puede ser vetado por listeners
    void Destroy();        // cierre inmediato sin veto
    void Focus();

    // ── estado ─────────────────────────────────────────────────────
    void Minimize();
    void Maximize();
    void Unmaximize();
    void Restore();
    void SetFullScreen(bool enabled);
    bool IsMaximized() const;
    bool IsMinimized() const;
    bool IsFullScreen() const;

    // ── estado extendido (paridad Electron) ────────────────────────
    bool IsVisible() const;
    bool IsFocused() const;
    bool IsResizable() const;
    bool IsMovable() const;
    bool IsMinimizable() const;
    bool IsMaximizable() const;
    bool IsClosable() const;
    bool IsAlwaysOnTop() const;
    bool IsKiosk() const;
    bool IsDestroyed() const;

    void SetResizable(bool on);
    void SetMovable(bool on);
    void SetMinimizable(bool on);
    void SetMaximizable(bool on);
    void SetClosable(bool on);
    void SetAlwaysOnTop(bool on, int level = 0);
    void SetSkipTaskbar(bool on);
    void SetHasShadow(bool on);
    void SetKiosk(bool on);
    void SetIgnoreMouseEvents(bool ignore, bool forward);
    /// value ∈ [-1,1] (-1 = indeterminado). mode: none|normal|indeterminate|paused|error
    void SetProgressBar(double value, const std::string& mode = "normal");
    void SetBackgroundColor(const std::string& color);
    void MoveTop();
    void SetAspectRatio(double ratio, int extraW = 0, int extraH = 0);

    // ── geometría extendida ────────────────────────────────────────
    Bounds GetContentBounds() const;
    void SetContentSize(int w, int h);
    Size GetContentSize() const;
    Size GetMinimumSize() const;
    Size GetMaximumSize() const;
    void SetMinimumSize(int w, int h);
    void SetMaximumSize(int w, int h);

    // ── geometría ──────────────────────────────────────────────────
    Bounds GetBounds() const;
    void SetBounds(const Bounds&);
    void Center();

    // ── apariencia / titlebar ───────────────────────────────────────
    void SetTitle(const std::string& title);
    std::string Title() const;
    void SetTitleBarStyle(TitleBarStyle style);
    void SetTitleBarOverlay(const TitleBarOverlay& overlay);

    // ── webviews embebidas (hijas de esta ventana) ─────────────────────
    // Cada una es un WebView independiente (con su proceso), embebido como hijo
    // y controlable por API. Linux de momento.
    /// Crea una webview hija. `optionsJson`:
    /// { url?, x?, y?, width?, height?, transparent?, userAgent? }.
    /// Devuelve JSON {"id":N}.
    std::string CreateWebview(const std::string& optionsJson);
    /// Control de una webview. `op` ∈ setBounds|load|back|forward|reload|stop|
    /// canBack|canForward|getURL|getTitle|eval|setVisible|setZoom|devtools|
    /// findInPage|findStop|destroy. Devuelve JSON con el resultado.
    std::string WebviewCommand(uint32_t id, const std::string& op,
                               const std::string& argsJson);

    /// Registra un esquema de ProtocolRegistry en el webview de esta ventana
    /// (se llama al crear la ventana y al registrar un esquema ya con ventanas).
    void RegisterProtocol(const std::string& scheme);

    /// Captura la página del WebView (PNG) de forma ASÍNCRONA.
    void CapturePage(std::function<void(bool ok, const std::string& png)> cb);

    /// Aplica (o quita) el menubar de aplicación. `itemsJson` = array de items
    /// (mismo formato que `menu.popup`). Windows: HMENU nativo; Linux: noop.
    void SetApplicationMenu(const std::string& itemsJson);

    /// Fuerza el esquema de color del contenido: 0=auto (sistema), 1=light, 2=dark.
    /// Windows: WebView2 `PreferredColorScheme`; Linux: noop (WebKitGTK no lo expone).
    void SetColorScheme(int mode);

    /// Exporta la página a PDF de forma asíncrona (`ok` + bytes PDF).
    void PrintToPDF(std::function<void(bool ok, const std::string& pdf)> cb);

    // ── webview ────────────────────────────────────────────────────
    void LoadURL(const std::string& url);
    /// Evalúa JS en la página. callback recibe el resultado JSON o null.
    void EvalJS(const std::string& js,
                std::function<void(std::string_view resultJson)> callback = nullptr);

    // ── eventos hacia JS (bridge) y C++ ─────────────────────────────
    /// Emite un evento a todos los suscriptores JS de esta ventana.
    void EmitToJS(const std::string& name, std::string_view jsonPayload);
    /// Suscriptor nativo. Los eventos nativos se reenvían a JS automáticamente.
    ListenerId On(const std::string& name, std::function<void(EventPayload)> cb);
    void Off(ListenerId id);

    /// Referencia cruda al backend de plataforma (solo para src/ interno).
    void* NativeHandle() const;

    /// Detrás de este puntero vive el estado de plataforma.
    class Impl;  // definido en src/Window/Window_p.hpp
    std::unique_ptr<Impl> impl_;

  public:
    /// Interno (ControlServer): respuesta del SDK al closeRequested.
    class Impl* impl() { return impl_.get(); }
};

} // namespace ow
