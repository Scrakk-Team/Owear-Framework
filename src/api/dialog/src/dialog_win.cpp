// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/dialog/src/dialog_win.cpp — IFileOpenDialog/IFileSaveDialog (COM).
// VERIFICAR-EN-WINDOWS.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace dlg {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static std::string WideToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void open(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string mode =
        parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
                parsed.value->AsArray()[0].IsString()
            ? parsed.value->AsArray()[0].AsString()
            : "open";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RAII: garantiza CoUninitialize() en cualquier punto de salida de la
    // función (sólo si CoInitializeEx tuvo éxito, incl. S_FALSE).
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) CoUninitialize(); }
    } comGuard{SUCCEEDED(hr)};
    bool multi = mode == "multi";
    bool save = mode == "save";
    bool dir = mode == "dir";

    IFileDialog* fd = nullptr;
    IID iid = save ? __uuidof(IFileSaveDialog) : __uuidof(IFileOpenDialog);
    if (FAILED(CoCreateInstance(save ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog),
                                nullptr, CLSCTX_INPROC_SERVER, iid, (void**)&fd))) {
        return RespondError(res, "COM falló");
    }
    DWORD opts;
    fd->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | (multi ? FOS_ALLOWMULTISELECT : 0) |
            (dir ? FOS_PICKFOLDERS : 0);
    fd->SetOptions(opts);

    Array results;
    auto finish = [&](bool ok_) {
        fd->Release();
        if (!ok_) RespondOk(res, "null");
    };

    hr = fd->Show(nullptr);
    if (FAILED(hr)) { finish(false); return; }

    if (multi) {
        IFileOpenDialog* fo = nullptr;
        if (SUCCEEDED(fd->QueryInterface(&fo)) && fo) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(fo->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &it)) && it) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                            results.emplace_back(Value(WideToUtf8(p)));
                            CoTaskMemFree(p);
                        }
                        it->Release();
                    }
                }
                items->Release();
            }
            fo->Release();
        }
        finish(true);
        RespondOk(res, Value(std::move(results)).Serialize().c_str());
        return;
    }

    IShellItem* item = nullptr;
    if (SUCCEEDED(fd->GetResult(&item)) && item) {
        PWSTR p = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            results.emplace_back(Value(WideToUtf8(p)));
            CoTaskMemFree(p);
        }
        item->Release();
    }
    finish(true);
    RespondOk(res, results.empty() ? "null" : results[0].Serialize().c_str());
}

void messageBox(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [type, title, message]");
    const auto& a = parsed.value->AsArray();

    UINT flags = MB_OK;
    std::string type = a[0].IsString() ? a[0].AsString() : "info";
    if (type == "warning") flags |= MB_ICONWARNING;
    else if (type == "error") flags |= MB_ICONERROR;
    else if (type == "question") { flags |= MB_YESNO | MB_ICONQUESTION; }

    auto toWide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    };

    int r = MessageBoxA(nullptr, a.size() > 2 && a[2].IsString()
                                      ? a[2].AsString().c_str() : "",
                        a.size() > 1 && a[1].IsString() ? a[1].AsString().c_str() : "",
                        flags);
    RespondOk(res, Value(static_cast<int64_t>(r)).Serialize().c_str());
}

// ── C2: API estilo Electron (options objects) ───────────────────────────────
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static bool HasProp(const Value& props, const char* name) {
    if (!props.IsArray()) return false;
    for (const auto& p : props.AsArray())
        if (p.IsString() && p.AsString() == name) return true;
    return false;
}

/// Diálogo de archivos COM. `out` recibe las rutas; devuelve false si canceló.
static bool RunFileDialog(const Value* opts, bool save, bool dir, bool multi,
                          std::vector<std::string>& out) {
    std::string title, defPath, buttonLabel;
    const Value* filters = nullptr;
    if (opts && opts->IsObject()) {
        if (const Value* v = opts->Find("title"); v && v->IsString()) title = v->AsString();
        if (const Value* v = opts->Find("defaultPath"); v && v->IsString())
            defPath = v->AsString();
        if (const Value* v = opts->Find("buttonLabel"); v && v->IsString())
            buttonLabel = v->AsString();
        filters = opts->Find("filters");
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) CoUninitialize(); }
    } comGuard{SUCCEEDED(hr)};

    IFileDialog* fd = nullptr;
    IID iid = save ? __uuidof(IFileSaveDialog) : __uuidof(IFileOpenDialog);
    if (FAILED(CoCreateInstance(save ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog),
                                nullptr, CLSCTX_INPROC_SERVER, iid, (void**)&fd)))
        return false;

    DWORD o;
    fd->GetOptions(&o);
    o |= FOS_FORCEFILESYSTEM | (multi ? FOS_ALLOWMULTISELECT : 0) |
         (dir ? FOS_PICKFOLDERS : 0);
    fd->SetOptions(o);
    if (!title.empty()) fd->SetTitle(ToWide(title).c_str());
    if (!buttonLabel.empty()) {
        IFileDialog2* fd2 = nullptr;
        if (SUCCEEDED(fd->QueryInterface(&fd2)) && fd2) {
            fd2->SetOkButtonLabel(ToWide(buttonLabel).c_str());
            fd2->Release();
        }
    }

    std::vector<std::wstring> fnames, fspecs;
    std::vector<COMDLG_FILTERSPEC> specs;
    if (filters && filters->IsArray()) {
        for (const auto& f : filters->AsArray()) {
            const Value* name = f.Find("name");
            const Value* exts = f.Find("extensions");
            if (!name || !name->IsString() || !exts || !exts->IsArray()) continue;
            std::string spec;
            bool first = true;
            for (const auto& e : exts->AsArray()) {
                if (!e.IsString()) continue;
                if (!first) spec += ";";
                spec += "*." + e.AsString();
                first = false;
            }
            if (spec.empty()) continue;
            fnames.push_back(ToWide(name->AsString()));
            fspecs.push_back(ToWide(spec));
        }
        for (size_t i = 0; i < fnames.size(); ++i)
            specs.push_back({fnames[i].c_str(), fspecs[i].c_str()});
        if (!specs.empty()) fd->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
    }

    if (!defPath.empty()) {
        if (save) {
            fd->SetFileName(ToWide(defPath).c_str());
        } else if (dir) {
            IShellItem* it = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(ToWide(defPath).c_str(), nullptr,
                                                      IID_PPV_ARGS(&it))) &&
                it) {
                fd->SetFolder(it);
                it->Release();
            }
        } else {
            std::filesystem::path p(defPath);
            IShellItem* it = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(
                    ToWide(p.parent_path().string()).c_str(), nullptr, IID_PPV_ARGS(&it))) &&
                it) {
                fd->SetFolder(it);
                it->Release();
            }
            fd->SetFileName(ToWide(p.filename().string()).c_str());
        }
    }

    hr = fd->Show(nullptr);
    if (FAILED(hr)) {
        fd->Release();
        return false;
    }

    if (multi) {
        IFileOpenDialog* fo = nullptr;
        if (SUCCEEDED(fd->QueryInterface(&fo)) && fo) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(fo->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &it)) && it) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                            out.push_back(WideToUtf8(p));
                            CoTaskMemFree(p);
                        }
                        it->Release();
                    }
                }
                items->Release();
            }
            fo->Release();
        }
    } else {
        IShellItem* item = nullptr;
        if (SUCCEEDED(fd->GetResult(&item)) && item) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out.push_back(WideToUtf8(p));
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    fd->Release();
    return true;
}

static const Value* FirstOpt(const ow_request_t* req,
                             std::unique_ptr<ow::json::Value>& holder) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return nullptr;
    holder = std::make_unique<ow::json::Value>(std::move(parsed.value->AsArray()[0]));
    return holder.get();
}

// args: [ { title?, defaultPath?, buttonLabel?, filters?, properties? } ]
//     → { canceled, filePaths: [..] }
void showOpenDialog(const ow_request_t* req, ow_response_t* res) {
    std::unique_ptr<ow::json::Value> holder;
    const Value* opts = FirstOpt(req, holder);
    bool dir = false, multi = false;
    if (opts && opts->IsObject()) {
        if (const Value* p = opts->Find("properties"); p) {
            dir = HasProp(*p, "openDirectory");
            multi = HasProp(*p, "multiSelections");
        }
    }
    std::vector<std::string> paths;
    const bool ok = RunFileDialog(opts, false, dir, multi, paths);

    Array arr;
    for (const auto& p : paths) arr.emplace_back(Value(p));
    Object o;
    o.emplace_back("canceled", Value(!ok || paths.empty()));
    o.emplace_back("filePaths", Value(std::move(arr)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// args: [ { title?, defaultPath?, buttonLabel?, filters? } ] → { canceled, filePath }
void showSaveDialog(const ow_request_t* req, ow_response_t* res) {
    std::unique_ptr<ow::json::Value> holder;
    const Value* opts = FirstOpt(req, holder);
    std::vector<std::string> paths;
    const bool ok = RunFileDialog(opts, true, false, false, paths);
    Object o;
    o.emplace_back("canceled", Value(!ok || paths.empty()));
    o.emplace_back("filePath", Value(paths.empty() ? std::string() : paths[0]));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// args: [ { type?, title?, message, detail?, buttons?, defaultId?, cancelId?,
//           checkboxLabel? } ] → { response, checkboxChecked }
//
// TaskDialogIndirect se resuelve DINÁMICAMENTE: importarlo estáticamente hace
// que dialog.dll falle al cargar (STATUS_ENTRYPOINT_NOT_FOUND) si el proceso no
// activa comctl32 v6 por manifiesto.
typedef HRESULT(WINAPI* TaskDialogIndirectFn)(const TASKDIALOGCONFIG*, int*, int*,
                                              BOOL*);
static TaskDialogIndirectFn ResolveTaskDialog() {
    static TaskDialogIndirectFn fn = []() -> TaskDialogIndirectFn {
        HMODULE h = LoadLibraryA("comctl32.dll");
        if (!h) return nullptr;
        return reinterpret_cast<TaskDialogIndirectFn>(
            reinterpret_cast<void*>(GetProcAddress(h, "TaskDialogIndirect")));
    }();
    return fn;
}

void showMessageBox(const ow_request_t* req, ow_response_t* res) {
    std::unique_ptr<ow::json::Value> holder;
    const Value* opts = FirstOpt(req, holder);
    if (!opts || !opts->IsObject()) return RespondError(res, "se espera [options]");

    std::string type = "info", title, message, detail, checkboxLabel;
    Array buttons;
    int defaultId = 0;
    if (const Value* v = opts->Find("type"); v && v->IsString()) type = v->AsString();
    if (const Value* v = opts->Find("title"); v && v->IsString()) title = v->AsString();
    if (const Value* v = opts->Find("message"); v && v->IsString()) message = v->AsString();
    if (const Value* v = opts->Find("detail"); v && v->IsString()) detail = v->AsString();
    if (const Value* v = opts->Find("buttons"); v && v->IsArray()) buttons = v->AsArray();
    if (const Value* v = opts->Find("defaultId"); v && v->IsNumber())
        defaultId = static_cast<int>(v->AsInt());
    if (const Value* v = opts->Find("checkboxLabel"); v && v->IsString())
        checkboxLabel = v->AsString();

    std::vector<std::wstring> wbtn;
    std::vector<TASKDIALOG_BUTTON> tbtn;
    for (const auto& b : buttons) {
        if (!b.IsString()) continue;
        wbtn.push_back(ToWide(b.AsString()));
        tbtn.push_back({static_cast<int>(tbtn.size()), wbtn.back().c_str()});
    }

    const std::wstring wtitle = ToWide(title);
    const std::wstring wmsg = ToWide(message);
    const std::wstring wdetail = ToWide(detail);
    const std::wstring wcheck = ToWide(checkboxLabel);

    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    if (!wtitle.empty()) cfg.pszWindowTitle = wtitle.c_str();
    cfg.pszMainInstruction = wmsg.c_str();
    if (!wdetail.empty()) cfg.pszContent = wdetail.c_str();
    if (type == "warning") cfg.pszMainIcon = TD_WARNING_ICON;
    else if (type == "error") cfg.pszMainIcon = TD_ERROR_ICON;
    else if (type == "question") cfg.pszMainIcon = TD_INFORMATION_ICON;
    else cfg.pszMainIcon = TD_INFORMATION_ICON;
    if (tbtn.empty()) {
        cfg.dwCommonButtons = TDCBF_OK_BUTTON;
    } else {
        cfg.cButtons = static_cast<UINT>(tbtn.size());
        cfg.pButtons = tbtn.data();
        cfg.nDefaultButton = defaultId;
    }
    BOOL checked = FALSE;
    if (!wcheck.empty()) cfg.pszVerificationText = wcheck.c_str();

    int btn = 0;
    TaskDialogIndirectFn taskDialog = ResolveTaskDialog();
    HRESULT hr = taskDialog ? taskDialog(&cfg, &btn, nullptr, &checked) : E_FAIL;
    if (FAILED(hr)) {
        // Fallback sin botones personalizados.
        int r = MessageBoxA(nullptr, message.c_str(), title.c_str(),
                            MB_OK | (type == "warning" ? MB_ICONWARNING
                                      : type == "error" ? MB_ICONERROR
                                                        : MB_ICONINFORMATION));
        btn = r;
        checked = FALSE;
    }

    Object o;
    o.emplace_back("response", Value(static_cast<int64_t>(btn)));
    o.emplace_back("checkboxChecked", Value(static_cast<bool>(checked)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace dlg

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"open", &dlg::open},
        {"messageBox", &dlg::messageBox},
        {"showOpenDialog", &dlg::showOpenDialog},
        {"showSaveDialog", &dlg::showSaveDialog},
        {"showMessageBox", &dlg::showMessageBox},
    };
    static const ow_module_desc_t d{
        "dialog", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
