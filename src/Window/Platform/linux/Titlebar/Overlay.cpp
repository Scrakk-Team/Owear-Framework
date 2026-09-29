// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Platform/linux/Titlebar.cpp — overlay nativo + resize por bordes.
#include "../Internal.hpp"
#include "../PlatformData.hpp"

#include <gtk/gtk.h>

#include <string>


namespace ow {

// ── overlay nativo (titleBarOverlay) ─────────────────────────────────────────
// Usamos los botones de ventana DEL TEMA (GTK `titlebutton`, los mismos que
// pinta GtkHeaderBar) para que el estilo, el tamaño y el espaciado los defina
// la distro. Nada de métricas hardcodeadas. Iconos simbólicos estándar de
// freedesktop (los mismos nombres que usa Electron en Linux).

void OnTbMinimize(GtkButton*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    if (impl->pdata && impl->pdata->window)
        gtk_window_iconify(GTK_WINDOW(impl->pdata->window));
}

void OnTbMaximize(GtkButton*, gpointer ud) {
    auto* impl = static_cast<Window::Impl*>(ud);
    if (!impl->pdata || !impl->pdata->window) return;
    GtkWindow* w = GTK_WINDOW(impl->pdata->window);
    if (gtk_window_is_maximized(w)) gtk_window_unmaximize(w);
    else gtk_window_maximize(w);
}

void OnTbClose(GtkButton*, gpointer ud) {
    static_cast<Window::Impl*>(ud)->BeginCloseFlow();
}

/// Cambia el icono del botón maximizar según el estado (nombre estándar).
void UpdateOverlayMaxIcon(Window::Impl* impl, bool maximized) {
    auto* pd = impl->pdata;
    if (!pd || !pd->overlayMaxBtn) return;
    GtkWidget* img = gtk_button_get_image(GTK_BUTTON(pd->overlayMaxBtn));
    if (img)
        gtk_image_set_from_icon_name(
            GTK_IMAGE(img),
            maximized ? "window-restore-symbolic" : "window-maximize-symbolic",
            GTK_ICON_SIZE_BUTTON);
}

/// Crea (idempotente) la barra con los botones del tema dentro del GtkOverlay.
void BuildOverlayBar(Window::Impl* impl) {
    auto* pd = impl->pdata;
    if (!pd || !pd->overlay || pd->overlayBar) return;
    if (!impl->opts.titleBarOverlay.enabled) return;

    const int h = impl->opts.titleBarOverlay.height > 0
                      ? impl->opts.titleBarOverlay.height
                      : 32;

    // GtkBox transparente del alto de banda (`height`). Los botones NO se
    // estiran: conservan su tamaño natural y se centran verticalmente, así el
    // estilo lo decide el tema de la distro.
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(box, "ow-titlebar-overlay"); // id para ganar especificidad
    gtk_widget_set_halign(box, GTK_ALIGN_END);
    gtk_widget_set_valign(box, GTK_ALIGN_START);
    gtk_widget_set_size_request(box, -1, h);

    GtkWidget* minb =
        gtk_button_new_from_icon_name("window-minimize-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* maxb =
        gtk_button_new_from_icon_name("window-maximize-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* closeb =
        gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* btns[3] = {minb, maxb, closeb};
    for (GtkWidget* b : btns) {
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "titlebutton");
        gtk_widget_set_valign(b, GTK_ALIGN_CENTER); // tamaño natural del tema
        gtk_widget_set_can_focus(b, FALSE);
        gtk_widget_set_focus_on_click(b, FALSE);
        gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(closeb), "close");

    g_signal_connect(minb, "clicked", G_CALLBACK(OnTbMinimize), impl);
    g_signal_connect(maxb, "clicked", G_CALLBACK(OnTbMaximize), impl);
    g_signal_connect(closeb, "clicked", G_CALLBACK(OnTbClose), impl);

    // Colores (todos opcionales; por defecto los del tema). El selector va por
    // id (`#ow-titlebar-overlay`) para ganar en especificidad a las reglas
    // `button.titlebutton` del tema.
    //  - color: fondo de la banda.
    //  - symbolColor: glifo.
    //  - buttonColor: fondo INTERNO del círculo (hover/pressed derivados).
    const std::string bg = CssHex(impl->opts.titleBarOverlay.color);
    const std::string btn = CssHex(impl->opts.titleBarOverlay.buttonColor);
    std::string fg = impl->opts.titleBarOverlay.symbolColor;
    if (fg.empty() && !btn.empty()) fg = ContrastHex(btn);
    else if (fg.empty() && !bg.empty()) fg = ContrastHex(bg);

    std::string css =
        "#ow-titlebar-overlay{background:transparent;box-shadow:none;border:none;}";
    if (!bg.empty()) css += "#ow-titlebar-overlay{background:" + bg + ";}";
    if (!fg.empty())
        css += "#ow-titlebar-overlay button.titlebutton{color:" + fg + ";}";
    if (!btn.empty()) {
        css += "#ow-titlebar-overlay button.titlebutton{background-image:none;"
               "background-color:" + btn + ";}";
        css += "#ow-titlebar-overlay button.titlebutton:hover{background-image:none;"
               "background-color:" + ShadeHex(btn, 0.08) + ";}";
        css += "#ow-titlebar-overlay button.titlebutton:active{background-image:none;"
               "background-color:" + ShadeHex(btn, -0.12) + ";}";
    }
    GtkCssProvider* prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, css.c_str(), -1, nullptr);
    // OJO: gtk_style_context_add_provider() en GTK3 solo afecta a ESE widget,
    // no a sus hijos. Los botones son hijos → hay que registrarlo a nivel de
    // pantalla (los selectores #id ya lo acotan).
    if (pd->cssProvider) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(pd->cssProvider));
        g_object_unref(pd->cssProvider);
    }
    pd->cssProvider = prov;
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    gtk_overlay_add_overlay(GTK_OVERLAY(pd->overlay), box);
    gtk_widget_show_all(box);

    pd->overlayBar = box;
    pd->overlayMaxBtn = maxb;
    UpdateOverlayMaxIcon(impl, GTK_WINDOW(pd->window) &&
                                   gtk_window_is_maximized(GTK_WINDOW(pd->window)));

    // Hueco reservado al renderer (el ancho lo mide GTK con el tema real).
    if (pd->webviewReady && impl->webview) {
        GtkRequisition req{};
        gtk_widget_get_preferred_size(box, nullptr, &req);
        const int w = req.width > 0 ? req.width : h * 3;
        impl->webview->InjectInitScript(
            "window.__owTitlebarOverlay={enabled:true,height:" +
            std::to_string(h) + ",width:" + std::to_string(w) +
            ",top:0,right:0};");
    }
}

void DestroyOverlayBar(Window::Impl* impl) {
    auto* pd = impl->pdata;
    if (!pd) return;
    if (pd->cssProvider) {
        gtk_style_context_remove_provider_for_screen(
            gdk_screen_get_default(), GTK_STYLE_PROVIDER(pd->cssProvider));
        g_object_unref(pd->cssProvider);
        pd->cssProvider = nullptr;
    }
    if (!pd->overlayBar) return;
    gtk_widget_destroy(pd->overlayBar);
    pd->overlayBar = nullptr;
    pd->overlayMaxBtn = nullptr;
}


} // namespace ow
