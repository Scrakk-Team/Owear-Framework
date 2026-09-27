// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_linux.cpp — icono de bandeja.
//
// Preferimos **AppIndicator (StatusNotifierItem)** cargándolo con `dlopen`
// (no requiere headers de dev; usa la lib en runtime: libayatana-appindicator3
// / libappindicator3). Es lo que funciona en GNOME/Zorin (con la extensión
// appindicator) y en KDE. Si no está la lib, caemos a **GtkStatusIcon** (X11
// clásico: XFCE/MATE/…).
//
// Icono: PNG base64 → archivo temporal → AppIndicator lo sirve por su tema.
// Menú: GtkMenu (template de Menu); los clicks emiten `menu.click`.
// Eventos: GtkStatusIcon da activate/right-click; AppIndicator no expone click
// (el icono abre el menú) → documentado.
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Menu.hpp"
#include "ow/Module.h"
#include "ow_api.h"

#include <dlfcn.h>
#include <gtk/gtk.h>

#include <cstdio>
#include <string>

namespace menu = ow::menu;

namespace tray {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static GtkStatusIcon* g_icon = nullptr;
static GtkWidget* g_menu = nullptr;
static std::string g_id = "owear-tray";
static unsigned g_iconSeq = 0;
static std::string g_iconPath;

// ── AppIndicator cargado con dlopen ─────────────────────────────────────────
struct AppIndicatorApi {
    bool ok = false;
    void* new_ = nullptr;          // AppIndicator* (*)(const char*, const char*, int)
    void* set_status = nullptr;    // void (*)(AppIndicator*, int)
    void* set_icon_full = nullptr; // void (*)(AppIndicator*, const char*, const char*)
    void* set_menu = nullptr;      // void (*)(AppIndicator*, GtkWidget*)
    void* set_title = nullptr;     // void (*)(AppIndicator*, const char*)
    void* set_label = nullptr;     // void (*)(AppIndicator*, const char*, const char*)
};
static AppIndicatorApi g_ai;
static void* g_aiInst = nullptr;

static void LoadAppIndicator() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    const char* names[] = {"libayatana-appindicator3.so.1",
                           "libappindicator3.so.1"};
    for (const char* n : names) {
        void* h = dlopen(n, RTLD_NOW | RTLD_GLOBAL);
        if (!h) continue;
        g_ai.new_ = dlsym(h, "app_indicator_new");
        g_ai.set_status = dlsym(h, "app_indicator_set_status");
        g_ai.set_icon_full = dlsym(h, "app_indicator_set_icon_full");
        g_ai.set_menu = dlsym(h, "app_indicator_set_menu");
        g_ai.set_title = dlsym(h, "app_indicator_set_title");
        g_ai.set_label = dlsym(h, "app_indicator_set_label");
        if (g_ai.new_ && g_ai.set_status && g_ai.set_icon_full && g_ai.set_menu) {
            g_ai.ok = true;
            return;
        }
        dlclose(h);
    }
}

static void Emit(const char* name, const std::string& payload) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, payload.c_str());
}

// ── menú contextual (misma semántica que el módulo `menu`) ──────────────────
static void OnItemActivate(GtkMenuItem* mi, gpointer) {
    const char* id = static_cast<const char*>(g_object_get_data(G_OBJECT(mi), "ow-id"));
    const char* role = static_cast<const char*>(g_object_get_data(G_OBJECT(mi), "ow-role"));
    menu::Item it;
    it.id = id ? id : "";
    it.role = role ? role : "";
    Emit("menu.click", menu::ClickPayload(it, 0));
}

static void BuildItems(GtkMenuShell* shell, const std::vector<menu::Item>& items,
                       GSList** radioGroup) {
    for (const menu::Item& it : items) {
        if (!it.visible) continue;
        if (it.type == "separator") {
            gtk_menu_shell_append(shell, gtk_separator_menu_item_new());
            continue;
        }
        const std::string label =
            it.accelerator.empty() ? it.label : it.label + "\t" + it.accelerator;
        GtkWidget* w = nullptr;
        if (it.type == "checkbox")
            w = gtk_check_menu_item_new_with_mnemonic(label.c_str());
        else if (it.type == "radio") {
            w = gtk_radio_menu_item_new_with_mnemonic(*radioGroup, label.c_str());
            *radioGroup = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
        } else
            w = gtk_menu_item_new_with_mnemonic(label.c_str());
        if (it.type == "checkbox" || it.type == "radio")
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), it.checked);
        if (!it.enabled) gtk_widget_set_sensitive(w, FALSE);
        g_object_set_data_full(G_OBJECT(w), "ow-id", g_strdup(it.id.c_str()), g_free);
        g_object_set_data_full(G_OBJECT(w), "ow-role", g_strdup(it.role.c_str()), g_free);
        gtk_menu_shell_append(shell, w);
        if (!it.submenu.empty()) {
            GtkWidget* subm = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(w), subm);
            GSList* rg = nullptr;
            BuildItems(GTK_MENU_SHELL(subm), it.submenu, &rg);
        } else {
            g_signal_connect(w, "activate", G_CALLBACK(OnItemActivate), nullptr);
        }
    }
}

static GtkWidget* BuildMenu(const Value& items) {
    GtkWidget* m = gtk_menu_new();
    GSList* rg = nullptr;
    BuildItems(GTK_MENU_SHELL(m), menu::ParseItems(items), &rg);
    gtk_widget_show_all(m);
    return m;
}

// ── icono ───────────────────────────────────────────────────────────────────
static void SetFromPngGtk(const std::string& b64) {
    if (!g_icon) return;
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(b64, png) || png.empty()) return;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, png.data(), png.size(), nullptr) ||
        !gdk_pixbuf_loader_close(loader, nullptr)) {
        g_object_unref(loader);
        return;
    }
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pb) gtk_status_icon_set_from_pixbuf(g_icon, pb);
    g_object_unref(loader);
}

/// AppIndicator resuelve el icono por un archivo; lo escribimos a un temp con
/// nombre nuevo cada vez para forzar el refresco.
static void SetFromPngAppIndicator(const std::string& b64) {
    if (!g_ai.ok || !g_aiInst) return;
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(b64, png) || png.empty()) return;
    const char* tmp = g_get_tmp_dir();
    std::string path = std::string(tmp ? tmp : "/tmp") + "/owear-tray-" + g_id + "-" +
                       std::to_string(++g_iconSeq) + ".png";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    g_iconPath = path;
    reinterpret_cast<void (*)(void*, const char*, const char*)>(g_ai.set_icon_full)(
        g_aiInst, path.c_str(), "");
}

static void OnActivate(GtkStatusIcon*, gpointer) {
    Emit("tray.event", "{\"button\":\"left\"}");
}
static void OnPopupMenu(GtkStatusIcon* icon, guint button, guint time, gpointer) {
    Emit("tray.event", "{\"button\":\"right\"}");
    if (g_menu)
        gtk_menu_popup(GTK_MENU(g_menu), nullptr, nullptr, gtk_status_icon_position_menu,
                       icon, button, time);
}

// ── API ─────────────────────────────────────────────────────────────────────
void create(const ow_request_t*, ow_response_t* res) {
    if (g_aiInst || g_icon) return RespondOk(res, "null");
    LoadAppIndicator();
    if (g_ai.ok) {
        g_aiInst = reinterpret_cast<void* (*)(const char*, const char*, int)>(g_ai.new_)(
            g_id.c_str(), "application-default-icon", 0 /*APPLICATION_STATUS*/);
        reinterpret_cast<void (*)(void*, int)>(g_ai.set_status)(g_aiInst,
                                                                1 /*ACTIVE*/);
    } else {
        g_icon = gtk_status_icon_new();
        gtk_status_icon_set_visible(g_icon, TRUE);
        g_signal_connect(g_icon, "activate", G_CALLBACK(OnActivate), nullptr);
        g_signal_connect(g_icon, "popup-menu", G_CALLBACK(OnPopupMenu), nullptr);
    }
    RespondOk(res, "\"owear-tray\"");
}

void setImage(const ow_request_t* req, ow_response_t* res) {
    if (!g_aiInst && !g_icon) return RespondError(res, "tray.create primero");
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [pngBase64]");
    const std::string& b64 = parsed.value->AsArray()[0].AsString();
    if (g_aiInst)
        SetFromPngAppIndicator(b64);
    else
        SetFromPngGtk(b64);
    RespondOk(res, "null");
}

void setPressedImage(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, "null");
}

void setToolTip(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string tip;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        tip = parsed.value->AsArray()[0].AsString();
    if (g_aiInst && g_ai.set_title)
        reinterpret_cast<void (*)(void*, const char*)>(g_ai.set_title)(g_aiInst, tip.c_str());
    else if (g_icon)
        gtk_status_icon_set_tooltip_text(g_icon, tip.c_str());
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string t;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        t = parsed.value->AsArray()[0].AsString();
    if (g_aiInst && g_ai.set_label)
        reinterpret_cast<void (*)(void*, const char*, const char*)>(g_ai.set_label)(
            g_aiInst, t.c_str(), t.c_str());
    RespondOk(res, "null");
}

void setContextMenu(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");
    const Value& a0 = parsed.value->AsArray()[0];
    const Value* items = a0.IsObject() ? a0.Find("items") : &a0;
    if (!items || !items->IsArray()) return RespondError(res, "items requeridos");
    if (g_menu) {
        gtk_widget_destroy(g_menu);
        g_menu = nullptr;
    }
    g_menu = BuildMenu(*items);
    if (g_aiInst && g_ai.set_menu)
        reinterpret_cast<void (*)(void*, GtkWidget*)>(g_ai.set_menu)(g_aiInst, g_menu);
    RespondOk(res, "null");
}

void popupContextMenu(const ow_request_t*, ow_response_t* res) {
    // AppIndicator gestiona el menú por sí mismo; GtkStatusIcon lo popula.
    if (g_menu && g_icon) {
        gtk_menu_popup(GTK_MENU(g_menu), nullptr, nullptr, gtk_status_icon_position_menu,
                       g_icon, 0, gtk_get_current_event_time());
    }
    RespondOk(res, "null");
}

void destroy(const ow_request_t*, ow_response_t* res) {
    if (g_menu) {
        gtk_widget_destroy(g_menu);
        g_menu = nullptr;
    }
    if (g_icon) {
        gtk_status_icon_set_visible(g_icon, FALSE);
        g_object_unref(g_icon);
        g_icon = nullptr;
    }
    // AppIndicator no tiene destroy público; se oculta (status PASSIVE).
    if (g_aiInst && g_ai.set_status)
        reinterpret_cast<void (*)(void*, int)>(g_ai.set_status)(g_aiInst, 0 /*PASSIVE*/);
    g_aiInst = nullptr;
    RespondOk(res, "null");
}

} // namespace tray

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    tray::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"create", &tray::create},
        {"setImage", &tray::setImage},
        {"setPressedImage", &tray::setPressedImage},
        {"setToolTip", &tray::setToolTip},
        {"setTitle", &tray::setTitle},
        {"setContextMenu", &tray::setContextMenu},
        {"popupContextMenu", &tray::popupContextMenu},
        {"destroy", &tray::destroy},
    };
    static const ow_module_desc_t d{"tray", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
