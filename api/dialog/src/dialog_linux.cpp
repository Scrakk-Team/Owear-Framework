// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/dialog/src/dialog_linux.cpp — GtkFileChooserNative + GtkMessageDialog.
// Los invokes corren en el main thread GTK: los diálogos modales son seguros.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gtk/gtk.h>

namespace dlg {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static GtkFileChooserAction ParseAction(const std::string& mode, bool* save) {
    *save = false;
    if (mode == "save") { *save = true; return GTK_FILE_CHOOSER_ACTION_SAVE; }
    if (mode == "dir") return GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    if (mode == "multi") return GTK_FILE_CHOOSER_ACTION_OPEN;
    return GTK_FILE_CHOOSER_ACTION_OPEN;
}

static void ApplyFilters(GtkFileChooser* chooser, const Value& filters) {
    if (!filters.IsArray()) return;
    for (const auto& f : filters.AsArray()) {
        const Value* name = f.Find("name");
        const Value* exts = f.Find("extensions");
        if (!name || !name->IsString() || !exts || !exts->IsArray()) continue;
        std::string pattern;
        bool first = true;
        for (const auto& e : exts->AsArray()) {
            if (!e.IsString()) continue;
            if (!first) pattern += ";";
            pattern += "*." + e.AsString();
            first = false;
        }
        if (pattern.empty()) continue;
        GtkFileFilter* ff = gtk_file_filter_new();
        gtk_file_filter_set_name(ff, name->AsString().c_str());
        gtk_file_filter_add_pattern(ff, pattern.c_str());
        gtk_file_filter_add_pattern(ff, "*." ); // fallback
        gtk_file_chooser_add_filter(chooser, ff);
    }
}

// args: [mode("open"|"multi"|"save"|"dir"), title?, defaultPath?, filters?]
void open(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray())
        return RespondError(res, "args inválidos");
    const auto& a = parsed.value->AsArray();
    std::string mode = a.size() > 0 && a[0].IsString() ? a[0].AsString() : "open";
    std::string title = a.size() > 1 && a[1].IsString() ? a[1].AsString() : "Abrir";
    std::string defPath = a.size() > 2 && a[2].IsString() ? a[2].AsString() : "";

    bool save = false;
    GtkFileChooserAction action = ParseAction(mode, &save);

    GtkWidget* dlgW = save ? gtk_file_chooser_dialog_new(
                                 title.c_str(), nullptr, action, "_Cancelar",
                                 GTK_RESPONSE_CANCEL, "_Guardar", GTK_RESPONSE_ACCEPT,
                                 nullptr)
                           : gtk_file_chooser_dialog_new(
                                 title.c_str(), nullptr, action, "_Cancelar",
                                 GTK_RESPONSE_CANCEL, "_Abrir", GTK_RESPONSE_ACCEPT,
                                 nullptr);
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dlgW);
    if (!defPath.empty()) gtk_file_chooser_set_current_name(chooser, defPath.c_str());
    else if (!defPath.empty()) {}
    if (a.size() > 2 && a[2].IsString() && !save)
        gtk_file_chooser_set_filename(chooser, defPath.c_str());
    if (a.size() > 3) ApplyFilters(chooser, a[3]);
    gtk_file_chooser_set_select_multiple(chooser, mode == "multi");

    gint resp = gtk_dialog_run(GTK_DIALOG(dlgW));
    if (resp != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlgW);
        return RespondOk(res, "null");
    }

    if (mode == "multi") {
        GSList* files = gtk_file_chooser_get_filenames(chooser);
        Array arr;
        for (GSList* it = files; it; it = it->next)
            arr.emplace_back(Value(std::string(static_cast<char*>(it->data))));
        g_slist_free_full(files, g_free);
        gtk_widget_destroy(dlgW);
        RespondOk(res, Value(std::move(arr)).Serialize().c_str());
        return;
    }

    gchar* file = gtk_file_chooser_get_filename(chooser);
    std::string out = file ? file : "";
    g_free(file);
    gtk_widget_destroy(dlgW);
    RespondOk(res, out.empty() ? "null" : Value(out).Serialize().c_str());
}

// args: [type("info"|"warning"|"error"|"question"), title, message, buttons?, detail?]
void messageBox(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [type, title, message]");
    const auto& a = parsed.value->AsArray();
    std::string type = a[0].IsString() ? a[0].AsString() : "info";
    std::string title = a[1].IsString() ? a[1].AsString() : "";
    std::string message = a[2].IsString() ? a[2].AsString() : "";
    Array buttons;
    if (a.size() > 3 && a[3].IsArray()) buttons = a[3].AsArray();

    GtkMessageType mt = GTK_MESSAGE_INFO;
    if (type == "warning") mt = GTK_MESSAGE_WARNING;
    else if (type == "error") mt = GTK_MESSAGE_ERROR;
    else if (type == "question") mt = GTK_MESSAGE_QUESTION;

    GtkWidget* w = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, mt,
                                          GTK_BUTTONS_NONE, "%s", message.c_str());
    gtk_window_set_title(GTK_WINDOW(w), title.c_str());
    int id = 0;
    if (buttons.empty()) {
        gtk_dialog_add_button(GTK_DIALOG(w), "_OK", 0);
    } else {
        for (const auto& b : buttons) {
            if (b.IsString())
                gtk_dialog_add_button(GTK_DIALOG(w), b.AsString().c_str(), id++);
        }
    }
    gint resp = gtk_dialog_run(GTK_DIALOG(w));
    gtk_widget_destroy(w);
    RespondOk(res, Value(static_cast<int64_t>(resp)).Serialize().c_str());
}

// ── C2: API estilo Electron (options objects) ───────────────────────────────
static bool HasProp(const Value& props, const char* name) {
    if (!props.IsArray()) return false;
    for (const auto& p : props.AsArray())
        if (p.IsString() && p.AsString() == name) return true;
    return false;
}

static GtkWidget* MakeChooser(const std::string& title, GtkFileChooserAction action,
                              const std::string& buttonLabel) {
    const char* ok = !buttonLabel.empty()
                         ? buttonLabel.c_str()
                         : (action == GTK_FILE_CHOOSER_ACTION_SAVE ? "_Guardar"
                                                                   : "_Abrir");
    return gtk_file_chooser_dialog_new(title.c_str(), nullptr, action, "_Cancelar",
                                       GTK_RESPONSE_CANCEL, ok, GTK_RESPONSE_ACCEPT,
                                       nullptr);
}

// args: [ { title?, defaultPath?, buttonLabel?, filters?, properties? } ]
//     → { canceled, filePaths: [..] }
void showOpenDialog(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    const Value* opts = nullptr;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty())
        opts = &parsed.value->AsArray()[0];
    std::string title = "Abrir", defPath, buttonLabel;
    const Value* props = nullptr;
    const Value* filters = nullptr;
    if (opts && opts->IsObject()) {
        if (const Value* v = opts->Find("title"); v && v->IsString()) title = v->AsString();
        if (const Value* v = opts->Find("defaultPath"); v && v->IsString()) defPath = v->AsString();
        if (const Value* v = opts->Find("buttonLabel"); v && v->IsString())
            buttonLabel = v->AsString();
        props = opts->Find("properties");
        filters = opts->Find("filters");
    }
    const bool dir = props && HasProp(*props, "openDirectory");
    const bool multi = props && HasProp(*props, "multiSelections");

    GtkWidget* w = MakeChooser(
        title, dir ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
        buttonLabel);
    GtkFileChooser* ch = GTK_FILE_CHOOSER(w);
    if (!defPath.empty()) gtk_file_chooser_set_current_folder(ch, defPath.c_str());
    if (props && HasProp(*props, "showHiddenFiles"))
        gtk_file_chooser_set_show_hidden(ch, TRUE);
    if (filters) ApplyFilters(ch, *filters);
    gtk_file_chooser_set_select_multiple(ch, multi);

    Array paths;
    bool canceled = true;
    if (gtk_dialog_run(GTK_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
        GSList* files = gtk_file_chooser_get_filenames(ch);
        for (GSList* it = files; it; it = it->next)
            paths.emplace_back(Value(std::string(static_cast<char*>(it->data))));
        g_slist_free_full(files, g_free);
        canceled = paths.empty();
    }
    gtk_widget_destroy(w);
    Object o;
    o.emplace_back("canceled", Value(canceled));
    o.emplace_back("filePaths", Value(std::move(paths)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// args: [ { title?, defaultPath?, buttonLabel?, filters? } ] → { canceled, filePath }
void showSaveDialog(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    const Value* opts = nullptr;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty())
        opts = &parsed.value->AsArray()[0];
    std::string title = "Guardar", defPath, buttonLabel;
    const Value* filters = nullptr;
    if (opts && opts->IsObject()) {
        if (const Value* v = opts->Find("title"); v && v->IsString()) title = v->AsString();
        if (const Value* v = opts->Find("defaultPath"); v && v->IsString()) defPath = v->AsString();
        if (const Value* v = opts->Find("buttonLabel"); v && v->IsString())
            buttonLabel = v->AsString();
        filters = opts->Find("filters");
    }
    GtkWidget* w =
        MakeChooser(title, GTK_FILE_CHOOSER_ACTION_SAVE, buttonLabel);
    GtkFileChooser* ch = GTK_FILE_CHOOSER(w);
    if (!defPath.empty()) {
        gchar* base = g_path_get_basename(defPath.c_str());
        gchar* dirn = g_path_get_dirname(defPath.c_str());
        gtk_file_chooser_set_current_name(ch, base);
        gtk_file_chooser_set_current_folder(ch, dirn);
        g_free(base);
        g_free(dirn);
    }
    if (filters) ApplyFilters(ch, *filters);

    std::string out;
    bool canceled = true;
    if (gtk_dialog_run(GTK_DIALOG(w)) == GTK_RESPONSE_ACCEPT) {
        gchar* file = gtk_file_chooser_get_filename(ch);
        if (file) {
            out = file;
            g_free(file);
            canceled = out.empty();
        }
    }
    gtk_widget_destroy(w);
    Object o;
    o.emplace_back("canceled", Value(canceled));
    o.emplace_back("filePath", Value(out));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// args: [ { type?, title?, message, detail?, buttons?, defaultId?, cancelId?,
//           checkboxLabel? } ] → { response, checkboxChecked }
void showMessageBox(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    const Value* opts = nullptr;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty())
        opts = &parsed.value->AsArray()[0];
    if (!opts || !opts->IsObject()) return RespondError(res, "se espera [options]");

    std::string type = "info", title, message, detail, checkboxLabel;
    Array buttons;
    int defaultId = 0, cancelId = -1;
    if (const Value* v = opts->Find("type"); v && v->IsString()) type = v->AsString();
    if (const Value* v = opts->Find("title"); v && v->IsString()) title = v->AsString();
    if (const Value* v = opts->Find("message"); v && v->IsString()) message = v->AsString();
    if (const Value* v = opts->Find("detail"); v && v->IsString()) detail = v->AsString();
    if (const Value* v = opts->Find("buttons"); v && v->IsArray()) buttons = v->AsArray();
    if (const Value* v = opts->Find("defaultId"); v && v->IsNumber())
        defaultId = static_cast<int>(v->AsInt());
    if (const Value* v = opts->Find("cancelId"); v && v->IsNumber())
        cancelId = static_cast<int>(v->AsInt());
    if (const Value* v = opts->Find("checkboxLabel"); v && v->IsString())
        checkboxLabel = v->AsString();

    GtkMessageType mt = GTK_MESSAGE_INFO;
    if (type == "warning") mt = GTK_MESSAGE_WARNING;
    else if (type == "error") mt = GTK_MESSAGE_ERROR;
    else if (type == "question") mt = GTK_MESSAGE_QUESTION;

    GtkWidget* w = gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, mt, GTK_BUTTONS_NONE,
                                          "%s", message.c_str());
    if (!title.empty()) gtk_window_set_title(GTK_WINDOW(w), title.c_str());
    if (!detail.empty())
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(w), "%s", detail.c_str());

    int id = 0;
    if (buttons.empty()) {
        gtk_dialog_add_button(GTK_DIALOG(w), "_OK", 0);
    } else {
        for (const auto& b : buttons)
            if (b.IsString()) gtk_dialog_add_button(GTK_DIALOG(w), b.AsString().c_str(), id++);
    }
    gtk_dialog_set_default_response(GTK_DIALOG(w), defaultId);

    GtkWidget* checkbox = nullptr;
    if (!checkboxLabel.empty()) {
        checkbox = gtk_check_button_new_with_label(checkboxLabel.c_str());
        GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(w));
        gtk_box_pack_start(GTK_BOX(area), checkbox, FALSE, FALSE, 0);
        gtk_widget_show(checkbox);
    }

    const gint resp = gtk_dialog_run(GTK_DIALOG(w));
    const bool checked =
        checkbox && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox));
    gtk_widget_destroy(w);
    (void)cancelId;

    Object o;
    o.emplace_back("response", Value(static_cast<int64_t>(resp)));
    o.emplace_back("checkboxChecked", Value(checked));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace dlg

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    using dlg::open;
    static const ow_fn_entry_t fns[] = {
        {"open", [](const ow_request_t* r, ow_response_t* rr) { dlg::open(r, rr); }},
        {"messageBox",
         [](const ow_request_t* r, ow_response_t* rr) { dlg::messageBox(r, rr); }},
        {"showOpenDialog",
         [](const ow_request_t* r, ow_response_t* rr) { dlg::showOpenDialog(r, rr); }},
        {"showSaveDialog",
         [](const ow_request_t* r, ow_response_t* rr) { dlg::showSaveDialog(r, rr); }},
        {"showMessageBox",
         [](const ow_request_t* r, ow_response_t* rr) { dlg::showMessageBox(r, rr); }},
    };
    // las lambdas sin captura decaen a puntero función ✓
    static const ow_module_desc_t d{
        "dialog", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
