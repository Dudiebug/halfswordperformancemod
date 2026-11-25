#pragma once
#include "core/perf_core.h"

namespace hsp {
// Reads <exe dir>/HSPerf.ini, writing a commented default file when there is none. Logs the values in use.
core::Settings LoadSettings();
}  // namespace hsp
