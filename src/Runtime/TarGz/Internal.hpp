// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/TarGz/Internal.hpp — helpers internos de tar.gz.
#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdint>
#include <string>

namespace ow::archive {
namespace archive_detail {

inline constexpr size_t kBlock = 512; ///< tamaño de bloque tar

bool IsZeroBlock(const uint8_t* b);
uint64_t ParseOctal(const uint8_t* p, size_t n);

/// Nombre del fichero (ustar: name + prefix).
std::string ParseName(const uint8_t* h);

class TarWriter {
public:
    explicit TarWriter(const std::filesystem::path& dest) : dest_(dest) {}

    bool Begin(const std::string& longName, const std::string& name, char type,
               uint64_t size, std::string& err) {
        std::string path = longName.empty() ? name : longName;
        // normaliza y anti-traversal
        std::filesystem::path rel(path);
        if (rel.is_absolute() || path.find("..") != std::string::npos) {
            err = "ruta peligrosa en tar: " + path;
            return false;
        }
        current_ = dest_ / rel;

        if (type == '5') { // directorio
            std::error_code ec;
            std::filesystem::create_directories(current_, ec);
            return true;
        }
        if (type == 'L') { // GNU longname: el "archivo" es el nombre siguiente
            longBuf_.clear();
            longBuf_.reserve(static_cast<size_t>(size));
            return true;
        }
        if (type == '0' || type == 0) {
            auto parent = current_.parent_path();
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            file_.open(current_, std::ios::binary | std::ios::trunc);
            if (!file_) { err = "no se pudo crear " + current_.string(); return false; }
            return true;
        }
        // symlink/hardlink/pax: solo consumir datos
        return true;
    }

    void Data(const uint8_t* p, size_t n) {
        if (collectingLong_) {
            longBuf_.append(reinterpret_cast<const char*>(p), n);
            return;
        }
        if (file_.is_open()) file_.write(reinterpret_cast<const char*>(p),
                                         static_cast<std::streamsize>(n));
    }

    bool End(std::string& err, std::string& outLongName,
             const std::function<void(const std::string&)>& onEntry) {
        if (file_.is_open()) {
            file_.close();
            // permisos ejecutables para bin/node se ajustan fuera
            std::error_code ec;
            std::filesystem::permissions(
                current_, std::filesystem::perms::owner_read |
                              std::filesystem::perms::owner_write |
                              std::filesystem::perms::group_read |
                              std::filesystem::perms::others_read,
                ec);
            if (onEntry) {
                auto rel = std::filesystem::relative(current_, dest_, ec);
                if (!ec) onEntry(rel.string());
            }
        }
        if (collectingLong_) {
            outLongName = longBuf_;
            longBuf_.clear();
            collectingLong_ = false;
        }
        (void)err;
        return true;
    }

    bool collectingLong_ = false;

private:
    std::filesystem::path dest_;
    std::filesystem::path current_;
    std::ofstream file_;
    std::string longBuf_;
};

} // namespace archive_detail
} // namespace ow::archive
