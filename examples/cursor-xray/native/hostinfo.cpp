// native/hostinfo.cpp — módulo nativo PROPIO (.owm).
//
// ¿Por qué existe este módulo? Porque el conjunto stock NO trae información del
// sistema operativo (hostname, CPUs, RAM, uptime). Eso vive en el SO y sólo se
// puede leer en nativo. Aquí escribes la primitiva UNA vez y el renderer la
// llama directo con `hostinfo.info()`.
//
// Cross-platform y sin librerías extra (compila con el build script tal cual):
//   Linux  → <sys/utsname.h> + <sys/sysinfo.h> + sysconf
//   macOS  → <sys/utsname.h> + sysctl
//   Windows→ <windows.h>

#include <ow/Json.h>
#include <ow/Module.h>
#include "ow_api.h"

#include <cstdint>
#include <string>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <sys/utsname.h>
  #include <unistd.h>
  #if defined(__APPLE__)
    #include <sys/sysctl.h>
    #include <sys/time.h>
    #include <ctime>
  #else
    #include <sys/sysinfo.h>
  #endif
#endif

using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondOk;

namespace {

struct Info {
    std::string os, hostname, release, arch;
    int64_t cpuCount = 0;
    int64_t totalMemMb = 0;
    int64_t freeMemMb = 0;
    int64_t uptimeSec = 0;
    int64_t pid = 0;
};

Info Collect() {
    Info i;
#if defined(_WIN32)
    i.os = "Windows";
    i.release = "nt";

    char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = sizeof(name);
    if (GetComputerNameA(name, &n)) i.hostname = name;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    i.cpuCount = static_cast<int64_t>(si.dwNumberOfProcessors);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) i.arch = "x64";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) i.arch = "arm64";
    else i.arch = "x86";

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        i.totalMemMb = static_cast<int64_t>(ms.ullTotalPhys / (1024ull * 1024ull));
        i.freeMemMb = static_cast<int64_t>(ms.ullAvailPhys / (1024ull * 1024ull));
    }
    i.uptimeSec = static_cast<int64_t>(GetTickCount64() / 1000ull);
    i.pid = static_cast<int64_t>(GetCurrentProcessId());
#else
    struct utsname u{};
    if (uname(&u) == 0) {
        i.os = u.sysname;
        i.hostname = u.nodename;
        i.release = u.release;
        i.arch = u.machine;
    }
    i.pid = static_cast<int64_t>(getpid());

  #if defined(__APPLE__)
    size_t sz = sizeof(int);
    int ncpu = 0;
    if (sysctlbyname("hw.ncpu", &ncpu, &sz, nullptr, 0) == 0) i.cpuCount = ncpu;

    uint64_t mem = 0;
    sz = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &sz, nullptr, 0) == 0)
        i.totalMemMb = static_cast<int64_t>(mem / (1024ull * 1024ull));

    struct timeval bt{};
    sz = sizeof(bt);
    if (sysctlbyname("kern.boottime", &bt, &sz, nullptr, 0) == 0)
        i.uptimeSec = static_cast<int64_t>(::time(nullptr) - bt.tv_sec);
    // freeMemMb: requiere host_statistics64; se omite a propósito.
  #else
    struct sysinfo si{};
    if (sysinfo(&si) == 0) {
        i.uptimeSec = static_cast<int64_t>(si.uptime);
        i.totalMemMb = static_cast<int64_t>((static_cast<uint64_t>(si.totalram) *
                                             si.mem_unit) / (1024ull * 1024ull));
        i.freeMemMb = static_cast<int64_t>((static_cast<uint64_t>(si.freeram) *
                                            si.mem_unit) / (1024ull * 1024ull));
    }
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu <= 0) ncpu = get_nprocs();
    i.cpuCount = ncpu;
  #endif
#endif
    return i;
}

// args: [] → objeto con la info del sistema.
void info(const ow_request_t*, ow_response_t* res) {
    Info i = Collect();
    Object o;
    o.emplace_back("os", Value(i.os));
    o.emplace_back("hostname", Value(i.hostname));
    o.emplace_back("release", Value(i.release));
    o.emplace_back("arch", Value(i.arch));
    o.emplace_back("cpuCount", Value(i.cpuCount));
    o.emplace_back("totalMemMb", Value(i.totalMemMb));
    o.emplace_back("freeMemMb", Value(i.freeMemMb));
    o.emplace_back("uptimeSec", Value(i.uptimeSec));
    o.emplace_back("pid", Value(i.pid));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace

OW_MODULE_BEGIN(hostinfo, "1.0.0")
OW_FN(info)
OW_MODULE_END()
