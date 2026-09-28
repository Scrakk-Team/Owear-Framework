// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Webview/IWebviewBackend.hpp — contrato interno entre Window y backends.
// Cada plataforma implementa esto en Webview/<plat>/. Cambiarlo rompe las 3.
#pragma once

#include "ow/Common.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ow {

/// Handler de mensajes entrantes desde JS (texto JSON del bridge).
using WebMessageHandler = std::function<void(std::string_view text)>;
/// Callback de evaluación JS: resultado como JSON ("null" si no hay).
using EvalCallback = std::function<void(std::string_view resultJson, bool ok)>;
/// Emisor de eventos del WebView hacia la ventana (p. ej. beforeInput).
using WebviewEventSink = std::function<void(const std::string& name, std::string_view json)>;
/// Callback de captura de página: `ok` + bytes PNG.
using CaptureCallback = std::function<void(bool ok, const std::string& png)>;
/// Callback de exportación a PDF: `ok` + bytes PDF.
using PrintCallback = std::function<void(bool ok, const std::string& pdf)>;

class IWebviewBackend {
public:
    virtual ~IWebviewBackend() = default;

    /// Crea el WebView nativo embebido en `parentNativeWindow` (opaco:
    /// GtkWindow* en Linux, HWND en Windows, NSView* en macOS).
    virtual bool Create(void* parentNativeWindow, const std::vector<std::string>& args) = 0;

    /// Script de inicialización del bridge (inyectado en cada documento).
    virtual void InjectInitScript(const std::string& js) = 0;

    /// Registra el receptor de mensajes JS→nativo.
    virtual void SetMessageHandler(WebMessageHandler handler) = 0;

    /// Registra el emisor de eventos del WebView (beforeInput, etc.).
    virtual void SetEventSink(WebviewEventSink /*sink*/) {}

    /// Captura la página (PNG) de forma ASÍNCRONA (sin pump anidado).
    virtual void CapturePage(CaptureCallback cb) {
        if (cb) cb(false, {});
    }

    /// Fuerza el esquema de color del contenido: 0=auto, 1=light, 2=dark.
    virtual void SetPreferredColorScheme(int /*scheme*/) {}

    /// Exporta la página a PDF de forma ASÍNCRONA (`ok` + bytes PDF).
    virtual void PrintToPDF(PrintCallback cb) {
        if (cb) cb(false, {});
    }

    virtual void LoadURL(const std::string& url) = 0;

    virtual void EvalJS(const std::string& js, EvalCallback cb = nullptr) = 0;

    /// Registra un scheme personalizado que sirve archivos locales.
    /// `root` es el directorio base; requests son scheme://<ruta-relativa>.
    virtual void RegisterAssetScheme(const std::string& scheme,
                                     const std::filesystem::path& root) = 0;

    /// Registra un esquema gestionado por ProtocolRegistry (dir o handler en el
    /// main). Los flags privileged se leen del propio registro.
    virtual void RegisterProtocol(const std::string& /*scheme*/) {}

    virtual void Resize(int x, int y, int w, int h) = 0;

    /// Referencia nativa cruda (GtkWidget*, etc) — para tests y debug.
    virtual void* NativeWidget() const = 0;
};

/// Factory por plataforma (definida en Webview/<plat>/).
std::unique_ptr<IWebviewBackend> CreateWebviewBackend();

} // namespace ow
