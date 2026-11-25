// Thread-safe file logger: <game exe dir>/HSPerf.log (overwritten each launch).
#pragma once

namespace hsp {
void LogInit();
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
}  // namespace hsp

#define HSP_LOG(...) ::hsp::Log(__VA_ARGS__)
