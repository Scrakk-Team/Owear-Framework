// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/api/installer/src/shortcuts_linux.cpp — integración de escritorio (Linux).
//
// - ChooseDir: selector de carpeta NATIVO (GTK), sin depender de módulos .owm.
// - CreateShortcuts/RemoveShortcuts: .desktop reales en
//   ~/.local/share/applications (+ ~/Desktop / autostart), chmod +x,
//   icono en hicolor y `update-desktop-database` para que salga en el cajón.
#include "Internal.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtk/gtk.h>
#include <unistd.h>

namespace installermod::platform {

namespace fs = std::filesystem;

static std::string Home() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? h : "/tmp";
}

/// Ejecuta un comando best-effort (ignora fallos; p. ej. sin update-desktop-database).
static void RunQuiet(const std::string& cmd) {
    if (std::system(cmd.c_str()) != 0) {
        // silencioso a propósito
    }
}

std::string ChooseDir(const std::string& title, const std::string& defaultPath) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        title.empty() ? "Elegir carpeta" : title.c_str(), nullptr,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "Cancelar", GTK_RESPONSE_CANCEL,
        "Elegir", GTK_RESPONSE_ACCEPT, nullptr);
    if (!defaultPath.empty()) {
        std::error_code ec;
        if (fs::is_directory(defaultPath, ec))
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), defaultPath.c_str());
        else
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), defaultPath.c_str());
    }
    std::string out;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (f) {
            out = f;
            g_free(f);
        }
    }
    gtk_widget_destroy(dlg);
    return out;
}

static std::string DesktopEntry(const std::string& appId, const std::string& appName,
                                const std::string& execPath, const std::string& iconName,
                                bool autostart) {
    std::string e = "[Desktop Entry]\n";
    e += "Type=Application\n";
    e += "Version=1.0\n";
    e += "Name=" + appName + "\n";
    e += "Comment=Instalado con Owear\n";
    e += "Exec=\"" + execPath + "\"\n";
    if (!iconName.empty()) e += "Icon=" + iconName + "\n";
    e += "Terminal=false\n";
    e += "Categories=Utility;\n";
    e += "StartupNotify=true\n";
    e += "StartupWMClass=" + appId + "\n";
    if (autostart) e += "X-GNOME-Autostart-enabled=true\n";
    return e;
}

static bool Write(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    f.close();
    fs::permissions(p, fs::perms::owner_all | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, ec);
    return true;
}

/// Copia el icono a hicolor y devuelve el nombre de icono a usar (appId), o "".
static std::string InstallIcon(const std::string& appId, const std::string& iconPath) {
    if (iconPath.empty()) return "";
    std::error_code ec;
    if (!fs::is_regular_file(iconPath, ec)) return "";
    const std::string ext = fs::path(iconPath).extension().string();
    const fs::path dst = fs::path(Home()) / ".local/share/icons/hicolor/256x256/apps" /
                         (appId + ext);
    fs::create_directories(dst.parent_path(), ec);
    fs::copy_file(iconPath, dst, fs::copy_options::overwrite_existing, ec);
    return ec ? "" : appId;
}

bool CreateShortcuts(const std::string& appId, const std::string& appName,
                     const std::string& execPath, const std::string& iconPath,
                     bool desktop, bool menu, bool startup) {
    if (execPath.empty()) return false;
    const std::string home = Home();
    const std::string iconName = InstallIcon(appId, iconPath);
    const std::string entry = DesktopEntry(appId, appName, execPath, iconName, false);
    bool ok = true;

    if (menu) {
        ok = Write(fs::path(home) / ".local/share/applications" / (appId + ".desktop"), entry) && ok;
        // refresca la base de datos de aplicaciones → aparece en el cajón
        RunQuiet("update-desktop-database '" + home + "/.local/share/applications' >/dev/null 2>&1");
    }
    if (desktop) {
        const fs::path d = fs::path(home) / "Desktop" / (appName + ".desktop");
        ok = Write(d, entry) && ok;
        // GNOME exige "confiar" en el lanzador; best-effort
        RunQuiet("gio set '" + d.string() + "' metadata::trusted true >/dev/null 2>&1");
    }
    if (startup) {
        ok = Write(fs::path(home) / ".config/autostart" / (appId + ".desktop"),
                   DesktopEntry(appId, appName, execPath, iconName, true)) && ok;
    }
    return ok;
}

bool RemoveShortcuts(const std::string& appId, const std::string& appName) {
    const std::string home = Home();
    std::error_code ec;
    fs::remove(fs::path(home) / ".local/share/applications" / (appId + ".desktop"), ec);
    fs::remove(fs::path(home) / "Desktop" / (appName + ".desktop"), ec);
    fs::remove(fs::path(home) / ".config/autostart" / (appId + ".desktop"), ec);
    for (const char* ext : {".png", ".svg", ".ico", ".jpg", ".jpeg"})
        fs::remove(fs::path(home) / ".local/share/icons/hicolor/256x256/apps" / (appId + ext), ec);
    RunQuiet("update-desktop-database '" + home + "/.local/share/applications' >/dev/null 2>&1");
    return true;
}

bool RegisterUninstall(const std::string&, const std::string&, const std::string&,
                       const std::string&, const std::string&, const std::string&) {
    return true;
}

bool UnregisterUninstall(const std::string&) { return true; }

bool LaunchDetached(const std::string& path, const std::vector<std::string>& args) {
    if (path.empty()) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // El binario lanzado es OTRA app Owear: no debe heredar el entorno del
        // instalador (OW_ASSETS_DIR/OW_APP_MAIN apuntan a la caché del
        // instalador, y harían que el lanzado cargue el UI del instalador en
        // vez de la app instalada).
        for (const char* k : {"OW_ASSETS_DIR", "OW_APP_MAIN", "OW_MODULES_DIR",
                              "OW_APP_WORKERS", "OW_MODE", "OW_APP_ID",
                              "OW_APP_VERSION"})
            ::unsetenv(k);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(path.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(path.c_str(), argv.data());
        _exit(127);
    }
    return true;
}

bool IsElevated() { return ::geteuid() == 0; }

} // namespace installermod::platform
