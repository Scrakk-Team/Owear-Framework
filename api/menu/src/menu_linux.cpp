// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_linux.cpp — menús GTK (popup) con template estilo Electron.
//
// setApplicationMenu es NOOP por diseño (GNOME no usa menubar; el IDE dibuja el
// suyo en web). El click se emite como `menu.click {id, role, windowId}` (al SDK
// del main y a los renderers).
//
#include "ow/Menu.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gtk/gtk.h>

namespace menu {
using ow::menu::Item;
using ow::menu::ParseItems;
using ow::menu::ClickPayload;

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static uint32_t g_win = 0;

static void OnItemActivate(GtkMenuItem* mi, gpointer) {
    if (!g_host || !g_host->emit_event) return;
    const char* id = static_cast<const char*>(g_object_get_data(G_OBJECT(mi), "ow-id"));
    const char* role =
        static_cast<const char*>(g_object_get_data(G_OBJECT(mi), "ow-role"));
    Item it;
    it.id = id ? id : "";
    it.role = role ? role : "";
    const std::string payload = ClickPayload(it, g_win);
    // window_id = 0 → SDK (main) + todas las ventanas
    g_host->emit_event(g_host->ctx, 0, "menu.click", payload.c_str());
}

static void BuildItems(GtkMenuShell* shell, const std::vector<Item>& items,
                       GSList** radioGroup) {
    for (const Item& it : items) {
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

// args: [ { items: [...], x?, y? } ]  (o legacy: [ items ])
void popup(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "args inválidos");
    const Value& a0 = parsed.value->AsArray()[0];
    const Value* itemsV = &a0;
    if (a0.IsObject()) itemsV = a0.Find("items");
    if (!itemsV || !itemsV->IsArray()) return RespondError(res, "items requeridos");
    g_win = req->window_id;

    GtkMenu* m = GTK_MENU(gtk_menu_new());
    GSList* rg = nullptr;
    BuildItems(GTK_MENU_SHELL(m), ParseItems(*itemsV), &rg);
    gtk_widget_show_all(GTK_WIDGET(m));

    // El popup se lanza desde un click asíncrono (botón del renderer): NO hay
    // "trigger event" de GDK, así que gtk_menu_popup_at_pointer(nullptr) falla.
    // Lo colocamos con un rect de 1px en la posición del cursor sobre el root
    // window (válido siempre).
    gint px = 0, py = 0;
    if (GdkDisplay* dpy = gdk_display_get_default()) {
        if (GdkSeat* seat = gdk_display_get_default_seat(dpy)) {
            if (GdkDevice* ptr = gdk_seat_get_pointer(seat))
                gdk_device_get_position(ptr, nullptr, &px, &py);
        }
    }
    GdkWindow* root = gdk_get_default_root_window();
    if (root) {
        GdkRectangle rect{px, py, 1, 1};
        // Evento sintético de trigger (no hay uno real: el click llegó async).
        GdkEvent* trigger = gdk_event_new(GDK_BUTTON_PRESS);
        trigger->button.window = GDK_WINDOW(g_object_ref(root));
        trigger->button.send_event = TRUE;
        trigger->button.time = GDK_CURRENT_TIME;
        trigger->button.x = px;
        trigger->button.y = py;
        trigger->button.x_root = px;
        trigger->button.y_root = py;
        trigger->button.button = 1;
        gtk_menu_popup_at_rect(m, root, &rect, GDK_GRAVITY_NORTH_WEST,
                               GDK_GRAVITY_NORTH_WEST, trigger);
        gdk_event_free(trigger);
    } else {
        gtk_menu_popup_at_pointer(m, nullptr);
    }
    RespondOk(res, "null"); // popup async: vive hasta selección
}

} // namespace menu

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    menu::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"popup", &menu::popup},
        {"setApplicationMenu",
         [](const ow_request_t*, ow_response_t* res) {
             // noop por diseño: en GNOME/Linux no imponemos menubar
             ow::Module::RespondOk(res, "\"noop\"");
         }},
    };
    static const ow_module_desc_t d{"menu", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
