// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_linux.cpp — StatusNotifierItem (SNI) NATIVO con GDBus.
//
// Implementamos el estándar org.kde.StatusNotifierItem a mano (sin
// libappindicator, que en algunos sistemas toma su fallback GtkStatusIcon y no
// registra). Funciona en GNOME/Zorin (extensión appindicator), KDE, y XFCE con
// plugin SNI. El menú contextual usa com.canonical.dbusmenu.
//
// Icono: PNG base64 → GdkPixbuf → IconPixmap (ARGB32). Menú: template de Menu;
// los clicks emiten `menu.click`. Métodos Activate/SecondaryActivate → tray.event.
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Menu.hpp"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>
#include <gtk/gtk.h>

#include <map>
#include <string>
#include <vector>

namespace menu = ow::menu;

namespace tray {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static GDBusConnection* g_bus = nullptr;
static guint g_itemReg = 0;
static guint g_menuReg = 0;
static std::string g_id = "owear-tray";
static std::string g_title = "Owear";

static std::string g_iconName;
static std::vector<uint8_t> g_iconArgb;
static int g_iconW = 0, g_iconH = 0;

static std::vector<menu::Item> g_items;      // template actual
static std::map<int, menu::Item> g_byId;     // id dbusmenu → item
static bool g_itemIsMenu = false;

static const char* kItemPath = "/StatusNotifierItem";
static const char* kMenuPath = "/MenuBar";
static const char* kItemIface = "org.kde.StatusNotifierItem";
static const char* kMenuIface = "com.canonical.dbusmenu";

static void Emit(const char* name, const std::string& payload) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0, name, payload.c_str());
}

// ── introspect XML ──────────────────────────────────────────────────────────
static const char* kItemXml =
    "<node>"
    "<interface name='org.kde.StatusNotifierItem'>"
    "<method name='Activate'><arg name='x' type='i' direction='in'/>"
    "<arg name='y' type='i' direction='in'/></method>"
    "<method name='SecondaryActivate'><arg name='x' type='i' direction='in'/>"
    "<arg name='y' type='i' direction='in'/></method>"
    "<method name='ContextMenu'><arg name='x' type='i' direction='in'/>"
    "<arg name='y' type='i' direction='in'/></method>"
    "<method name='Scroll'><arg name='delta' type='i' direction='in'/>"
    "<arg name='orientation' type='s' direction='in'/></method>"
    "<property name='Category' type='s' access='read'/>"
    "<property name='Id' type='s' access='read'/>"
    "<property name='Title' type='s' access='read'/>"
    "<property name='Status' type='s' access='read'/>"
    "<property name='IconName' type='s' access='read'/>"
    "<property name='IconPixmap' type='a(iiay)' access='read'/>"
    "<property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
    "<property name='Menu' type='o' access='read'/>"
    "<property name='ItemIsMenu' type='b' access='read'/>"
    "<signal name='NewIcon'/>"
    "<signal name='NewStatus'><arg name='status' type='s'/></signal>"
    "</interface></node>";

static const char* kMenuXml =
    "<node>"
    "<interface name='com.canonical.dbusmenu'>"
    "<method name='GetLayout'><arg type='i' direction='in'/>"
    "<arg type='i' direction='in'/><arg type='as' direction='in'/>"
    "<arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/></method>"
    "<method name='GetGroupProperties'><arg type='ai' direction='in'/>"
    "<arg type='as' direction='in'/><arg type='a(ia{sv})' direction='out'/></method>"
    "<method name='Event'><arg type='i' direction='in'/>"
    "<arg type='s' direction='in'/><arg type='v' direction='in'/>"
    "<arg type='u' direction='in'/></method>"
    "<method name='EventGroup'><arg type='a(isvu)' direction='in'/>"
    "<arg type='ai' direction='out'/></method>"
    "<method name='AboutToShow'><arg type='i' direction='in'/>"
    "<arg type='b' direction='out'/></method>"
    "<method name='AboutToShowGroup'><arg type='ai' direction='in'/>"
    "<arg type='ai' direction='out'/><arg type='ai' direction='out'/></method>"
    "<property name='Version' type='u' access='read'/>"
    "<property name='Status' type='s' access='read'/>"
    "<signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
    "<signal name='ItemsPropertiesUpdated'>"
    "<arg type='a(ia{sv})'/><arg type='a(ias)'/></signal>"
    "</interface></node>";

// ── asignación de ids (mismo orden que el layout) ───────────────────────────
static void AssignIds(const std::vector<menu::Item>& items, int& counter) {
    for (const auto& it : items) {
        const int id = ++counter;
        g_byId[id] = it;
        if (!it.submenu.empty()) AssignIds(it.submenu, counter);
    }
}

static void AddChildren(GVariantBuilder* children, const std::vector<menu::Item>& items,
                        int& counter) {
    for (const auto& it : items) {
        const int id = ++counter;
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(it.label.c_str()));
        g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(it.enabled));
        g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(it.visible));
        if (it.type == "separator")
            g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("separator"));
        if (it.type == "checkbox") {
            g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string("checkmark"));
            g_variant_builder_add(&props, "{sv}", "toggle-state",
                                  g_variant_new_int32(it.checked ? 1 : 0));
        }
        if (it.type == "radio") {
            g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string("radio"));
            g_variant_builder_add(&props, "{sv}", "toggle-state",
                                  g_variant_new_int32(it.checked ? 1 : 0));
        }
        if (!it.submenu.empty())
            g_variant_builder_add(&props, "{sv}", "children-display",
                                  g_variant_new_string("submenu"));

        GVariantBuilder childArr;
        g_variant_builder_init(&childArr, G_VARIANT_TYPE("av"));
        if (!it.submenu.empty()) AddChildren(&childArr, it.submenu, counter);

        GVariant* node = g_variant_new("(i@a{sv}@av)", id, g_variant_builder_end(&props),
                                       g_variant_builder_end(&childArr));
        g_variant_builder_add(children, "v", node);
    }
}

// ── handlers ────────────────────────────────────────────────────────────────
static void OnItemMethodCall(GDBusConnection*, const gchar*, const gchar*,
                             const gchar* iface, const gchar* method, GVariant*,
                             GDBusMethodInvocation* inv, gpointer) {
    (void)iface;
    if (g_strcmp0(method, "Activate") == 0)
        Emit("tray.event", "{\"button\":\"left\"}");
    else if (g_strcmp0(method, "SecondaryActivate") == 0)
        Emit("tray.event", "{\"button\":\"middle\"}");
    else if (g_strcmp0(method, "ContextMenu") == 0)
        Emit("tray.event", "{\"button\":\"right\"}");
    // Scroll: ignorado
    g_dbus_method_invocation_return_value(inv, nullptr);
}

static GVariant* OnItemGetProperty(GDBusConnection*, const gchar*, const gchar*,
                                   const gchar*, const gchar* property, GError**,
                                   gpointer) {
    if (g_strcmp0(property, "Category") == 0)
        return g_variant_new_string("ApplicationStatus");
    if (g_strcmp0(property, "Id") == 0) return g_variant_new_string(g_id.c_str());
    if (g_strcmp0(property, "Title") == 0) return g_variant_new_string(g_title.c_str());
    if (g_strcmp0(property, "Status") == 0) return g_variant_new_string("Active");
    if (g_strcmp0(property, "IconName") == 0) return g_variant_new_string(g_iconName.c_str());
    if (g_strcmp0(property, "IconPixmap") == 0) {
        GVariantBuilder arr;
        g_variant_builder_init(&arr, G_VARIANT_TYPE("a(iiay)"));
        if (g_iconW > 0 && !g_iconArgb.empty()) {
            GVariant* bytes = g_variant_new_fixed_array(
                G_VARIANT_TYPE_BYTE, g_iconArgb.data(), g_iconArgb.size(), 1);
            g_variant_builder_add(&arr, "(ii@ay)", g_iconW, g_iconH, bytes);
        }
        return g_variant_builder_end(&arr);
    }
    if (g_strcmp0(property, "ToolTip") == 0) {
        GVariantBuilder pix; g_variant_builder_init(&pix, G_VARIANT_TYPE("a(iiay)"));
        return g_variant_new("(s@a(iiay)ss)", g_title.c_str(), g_variant_builder_end(&pix),
                             "", "");
    }
    if (g_strcmp0(property, "Menu") == 0)
        return g_variant_new_object_path(kMenuPath);
    if (g_strcmp0(property, "ItemIsMenu") == 0) return g_variant_new_boolean(g_itemIsMenu);
    return nullptr;
}

static void OnMenuMethodCall(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                             const gchar* method, GVariant* params,
                             GDBusMethodInvocation* inv, gpointer) {
    if (g_strcmp0(method, "GetLayout") == 0) {
        int counter = 0;
        GVariantBuilder empty;
        g_variant_builder_init(&empty, G_VARIANT_TYPE("a{sv}"));
        GVariantBuilder children;
        g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        AddChildren(&children, g_items, counter);
        GVariant* root = g_variant_new("(i@a{sv}@av)", 0, g_variant_builder_end(&empty),
                                       g_variant_builder_end(&children));
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(u@(ia{sv}av))", (guint32)1, root));
        return;
    }
    if (g_strcmp0(method, "Event") == 0) {
        gint id = 0;
        const gchar* ev = "";
        g_variant_get(params, "(i&svu)", &id, &ev, nullptr, nullptr);
        if (g_strcmp0(ev, "clicked") == 0) {
            auto it = g_byId.find(id);
            if (it != g_byId.end()) Emit("menu.click", menu::ClickPayload(it->second, 0));
        }
        g_dbus_method_invocation_return_value(inv, nullptr);
        return;
    }
    if (g_strcmp0(method, "AboutToShow") == 0) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
        return;
    }
    if (g_strcmp0(method, "GetGroupProperties") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("a(ia{sv})"));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a(ia{sv}))",
                                                                 g_variant_builder_end(&b)));
        return;
    }
    if (g_strcmp0(method, "EventGroup") == 0) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ai)",
                                                                 g_variant_builder_end(&b)));
        return;
    }
    if (g_strcmp0(method, "AboutToShowGroup") == 0) {
        GVariantBuilder b1, b2;
        g_variant_builder_init(&b1, G_VARIANT_TYPE("ai"));
        g_variant_builder_init(&b2, G_VARIANT_TYPE("ai"));
        g_dbus_method_invocation_return_value(
            inv, g_variant_new("(@ai@ai)", g_variant_builder_end(&b1),
                               g_variant_builder_end(&b2)));
        return;
    }
    g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod",
                                               "método no soportado");
}

static GVariant* OnMenuGetProperty(GDBusConnection*, const gchar*, const gchar*,
                                   const gchar*, const gchar* property, GError**,
                                   gpointer) {
    if (g_strcmp0(property, "Version") == 0) return g_variant_new_uint32(3);
    if (g_strcmp0(property, "Status") == 0) return g_variant_new_string("normal");
    return nullptr;
}

// ── icono ───────────────────────────────────────────────────────────────────
static void SetIconFromPng(const std::string& b64) {
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(b64, png) || png.empty()) return;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!gdk_pixbuf_loader_write(loader, png.data(), png.size(), nullptr) ||
        !gdk_pixbuf_loader_close(loader, nullptr)) {
        g_object_unref(loader);
        return;
    }
    GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader);
    if (pb) {
        const int w = gdk_pixbuf_get_width(pb);
        const int h = gdk_pixbuf_get_height(pb);
        const int nch = gdk_pixbuf_get_n_channels(pb);
        const int stride = gdk_pixbuf_get_rowstride(pb);
        const guchar* px = gdk_pixbuf_get_pixels(pb);
        g_iconArgb.assign(static_cast<size_t>(w) * h * 4, 0);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const guchar* s = px + y * stride + x * nch;
                const uint8_t r = s[0];
                const uint8_t g = nch > 2 ? s[1] : s[0];
                const uint8_t b = nch > 2 ? s[2] : s[0];
                const uint8_t a = nch > 3 ? s[3] : 255;
                const size_t o = (static_cast<size_t>(y) * w + x) * 4;
                g_iconArgb[o] = a;      // ARGB32 network order: A,R,G,B
                g_iconArgb[o + 1] = r;
                g_iconArgb[o + 2] = g;
                g_iconArgb[o + 3] = b;
            }
        }
        g_iconW = w;
        g_iconH = h;
    }
    g_object_unref(loader);
}

static void EmitNewIcon() {
    if (!g_bus) return;
    g_dbus_connection_emit_signal(g_bus, nullptr, kItemPath, kItemIface, "NewIcon",
                                  nullptr, nullptr);
}

// ── API ─────────────────────────────────────────────────────────────────────
void create(const ow_request_t*, ow_response_t* res) {
    if (g_itemReg) return RespondOk(res, "null");
    GError* err = nullptr;
    g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!g_bus) {
        std::string m = err ? err->message : "sin bus de sesión";
        if (err) g_error_free(err);
        return RespondError(res, m);
    }

    GDBusNodeInfo* itemInfo = g_dbus_node_info_new_for_xml(kItemXml, nullptr);
    GDBusNodeInfo* menuInfo = g_dbus_node_info_new_for_xml(kMenuXml, nullptr);
    static const GDBusInterfaceVTable itemVtable{&OnItemMethodCall, &OnItemGetProperty,
                                                 nullptr};
    static const GDBusInterfaceVTable menuVtable{&OnMenuMethodCall, &OnMenuGetProperty,
                                                 nullptr};

    g_itemReg = g_dbus_connection_register_object(
        g_bus, kItemPath, itemInfo->interfaces[0], &itemVtable, nullptr, nullptr, &err);
    g_menuReg = g_dbus_connection_register_object(
        g_bus, kMenuPath, menuInfo->interfaces[0], &menuVtable, nullptr, nullptr, &err);
    g_dbus_node_info_unref(itemInfo);
    g_dbus_node_info_unref(menuInfo);
    if (!g_itemReg || !g_menuReg)
        return RespondError(res, "no se pudo registrar el objeto D-Bus");

    // Registrar con el watcher (pasamos el path; el watcher usa nuestro nombre).
    g_dbus_connection_call(g_bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                           "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
                           g_variant_new("(s)", kItemPath), nullptr, G_DBUS_CALL_FLAGS_NONE,
                           -1, nullptr, nullptr, nullptr);
    RespondOk(res, "null");
}

void setImage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "se espera [pngBase64]");
    SetIconFromPng(parsed.value->AsArray()[0].AsString());
    EmitNewIcon();
    RespondOk(res, "null");
}

void setPressedImage(const ow_request_t*, ow_response_t* res) { RespondOk(res, "null"); }

void setToolTip(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        g_title = parsed.value->AsArray()[0].AsString();
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) { setToolTip(req, res); }

void setContextMenu(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");
    const Value& a0 = parsed.value->AsArray()[0];
    const Value* items = a0.IsObject() ? a0.Find("items") : &a0;
    if (!items || !items->IsArray()) return RespondError(res, "items requeridos");
    g_items = menu::ParseItems(*items);
    g_byId.clear();
    int counter = 0;
    AssignIds(g_items, counter);
    g_itemIsMenu = !g_items.empty();
    RespondOk(res, "null");
}

void popupContextMenu(const ow_request_t*, ow_response_t* res) {
    if (g_bus)
        g_dbus_connection_emit_signal(g_bus, nullptr, kItemPath, kItemIface, "ContextMenu",
                                      nullptr, nullptr);
    RespondOk(res, "null");
}

void destroy(const ow_request_t*, ow_response_t* res) {
    if (g_bus) {
        if (g_itemReg) g_dbus_connection_unregister_object(g_bus, g_itemReg);
        if (g_menuReg) g_dbus_connection_unregister_object(g_bus, g_menuReg);
    }
    g_itemReg = g_menuReg = 0;
    if (g_bus) {
        g_object_unref(g_bus);
        g_bus = nullptr;
    }
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
