#pragma once
#include "core/perf_core.h"

namespace hsp {
// Reads <exe dir>/HSPerf.ini, writing a commented default file when there is none. Logs the values in use.
core::Settings LoadSettings();
// Writes HSPerf.ini with these values (the menu, after each change). False when the file could not be written.
bool SaveSettings(const core::Settings& s);
}  // namespace hsp
