#!/bin/bash
# Cross-compiles the mod as winhttp.dll with llvm-mingw (Linux or Git Bash on Windows; no MSVC needed).
# Usage: ./build.sh [Release|Debug]   Output: build/winhttp.dll
# LLVM_MINGW: the toolchain folder (default: ~/tools/llvm-mingw-* or ~/.local/opt/llvm-mingw-*, newest).
set -euo pipefail
cd "$(dirname "$0")"
TC=${LLVM_MINGW:-$(ls -d "$HOME"/tools/llvm-mingw-* "$HOME"/.local/opt/llvm-mingw-* 2>/dev/null | sort | tail -1)}
[ -n "$TC" ] && [ -d "$TC" ] || { echo "build: llvm-mingw not found; set LLVM_MINGW" >&2; exit 2; }
EXE=$([ -e "$TC/bin/x86_64-w64-mingw32-clang++.exe" ] && echo .exe || true)
CXX="$TC/bin/x86_64-w64-mingw32-clang++$EXE"
CC="$TC/bin/x86_64-w64-mingw32-clang$EXE"
CFG=${1:-Release}
OPT=$([ "$CFG" = Debug ] && echo "-O0 -g" || echo "-O2")
VERSION=$(cat VERSION)
DESCRIBE=$(git describe --always --dirty --exclude='*' 2>/dev/null | tr -cd 'A-Za-z0-9._+-' || true)
python3 tools/gen_winhttp_proxy.py --check >/dev/null || python tools/gen_winhttp_proxy.py --check >/dev/null || {
  echo "build: src/winhttp.def is out of date; run tools/gen_winhttp_proxy.py" >&2; exit 1; }
mkdir -p build/obj
COMMON="$OPT -Wall -Wextra -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -fstack-protector-strong"
objs=()
for f in src/*.cpp src/*/*.cpp; do
  [ -e "$f" ] || continue
  o="build/obj/$(echo "${f%.cpp}" | tr '/' '_').o"
  "$CXX" -std=c++20 $COMMON -Werror -Isrc -DHSP_VERSION="\"$VERSION${DESCRIBE:+ ($DESCRIBE)}\"" -c "$f" -o "$o"
  objs+=("$o")
done
"$CXX" -shared -o build/winhttp.dll "${objs[@]}" src/winhttp.def \
  -fstack-protector-strong -s -Wl,-Map,build/winhttp.map -static -static-libgcc -static-libstdc++ -Wl,--kill-at
echo "built build/winhttp.dll $VERSION ($CFG)"
