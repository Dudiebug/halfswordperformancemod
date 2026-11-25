#include "log.h"
#include "paths.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hsp {
namespace {
std::mutex g_mu;
FILE* g_file = nullptr;
}  // namespace

void LogInit() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_file) return;
    g_file = _wfopen(ModFilePath(L"HSPerf.log").c_str(), L"w");
}

void Log(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_file) return;
    std::fprintf(g_file, "%02u:%02u:%02u.%03u %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
    std::fflush(g_file);
}

}  // namespace hsp
