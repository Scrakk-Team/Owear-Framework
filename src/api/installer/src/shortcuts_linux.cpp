// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/api/installer/src/shortcuts_linux.cpp — integración de escritorio (Linux).
#include "Internal.hpp"

#include "../../../Core/Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace installermod::platform {

namespace fs = std::filesystem;

static std::string Home() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? h : "/tmp";
}

static std::string DesktopEntry(const std::string& appName, const std::string& execPath,
                                const std::string& iconPath, bool autostart) {
    std::string e;
    e += "[Desktop Entry]\n";
    e += "Type=Application\n";
    e += "Name=" + appName + "\n";
    e += "Exec=\"" + execPath + "\"\n";
    if (!iconPath.empty()) e += "Icon=" + iconPath + "\n";
    e += "Terminal=false\n";
    e += "Categories=Utility;\n";
    e += autostart ? "X-GNOME-Autostart-enabled=true\n" : "";
    return e;
}

static bool Write(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << data;
    fs::permissions(p, fs::perms::owner_all, fs::perm_options::replace, ec);
    return static_cast<bool>(f);
}

bool CreateShortcuts(const std::string& appId, const std::string& appName,
                     const std::string& execPath, const std::string& iconPath,
                     bool desktop, bool menu, bool startup) {
    if (execPath.empty()) return false;
    const std::string home = Home();
    bool ok = true;
    if (menu)
        ok = Write(fs::path(home) / ".local/share/applications" / (appId + ".desktop"),
                   DesktopEntry(appName, execPath, iconPath, false)) && ok;
    if (desktop)
        ok = Write(fs::path(home) / "Desktop" / (appName + ".desktop"),
                   DesktopEntry(appName, execPath, iconPath, false)) && ok;
    if (startup)
        ok = Write(fs::path(home) / ".config/autostart" / (appId + ".desktop"),
                   DesktopEntry(appName, execPath, iconPath, true)) && ok;
    return ok;
}

bool RemoveShortcuts(const std::string& appId, const std::string& appName) {
    const std::string home = Home();
    std::error_code ec;
    fs::remove(fs::path(home) / ".local/share/applications" / (appId + ".desktop"), ec);
    fs::remove(fs::path(home) / "Desktop" / (appName + ".desktop"), ec);
    fs::remove(fs::path(home) / ".config/autostart" / (appId + ".desktop"), ec);
    return true;
}

bool RegisterUninstall(const std::string&, const std::string&, const std::string&,
                       const std::string&, const std::string&, const std::string&) {
    // Linux no tiene registro central de desinstalación; el .desktop + el
    // manifiesto de instalación son la fuente de verdad.
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
