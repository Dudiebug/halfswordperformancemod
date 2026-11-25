#!/bin/bash
# Builds and runs the unit tests with the host compiler (CXX, default c++/g++/clang++; llvm-mingw's clang++ works on
# Windows when its bin folder is on PATH).
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-$(command -v c++ || command -v g++ || command -v clang++ || true)}
[ -n "$CXX" ] || { echo "tests: no C++ compiler; set CXX" >&2; exit 2; }
mkdir -p build
"$CXX" -std=c++20 -Wall -Wextra -Werror -O1 -Isrc tests/test_core.cpp -o build/test_core
./build/test_core
python3 tools/gen_winhttp_proxy.py --check 2>/dev/null || python tools/gen_winhttp_proxy.py --check
# Windows only: the built proxy loads and forwards to System32's winhttp.dll.
if [ -e build/winhttp.dll ] && [ "${OS:-}" = Windows_NT ]; then
  MCXX=$(command -v x86_64-w64-mingw32-clang++ || true)
  [ -n "$MCXX" ] && "$MCXX" -O1 tests/proxy_smoke.cpp -o build/proxy_smoke.exe -static && ./build/proxy_smoke.exe
fi
