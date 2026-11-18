#!/bin/bash
# Builds the Windows 98 SE executable (out/mksc98.exe) and runs the post-build
# gate (import check against Win98's DLLs, SSE/AVX disassembly scan).
# Usage, from WSL or any Linux with i686-w64-mingw32-gcc, cmake, ninja, python3:
#   ./build.sh            Win98 .exe
#   ./build.sh host       headless reference build for this machine (null backend)
#   ./build.sh profile    Win98 .exe with per-phase timers always compiled in
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
mode="${1:-win98}"
touch "$here/main_win98.c"   # the log's build stamp must name this build
case "$mode" in
  host)
    build="$HOME/mksc-build/host98"; mkdir -p "$build"
    cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$build.configure.log" 2>&1 ||
        { tail -30 "$build.configure.log"; exit 1; }
    cmake --build "$build" --parallel "$(nproc)" 2>&1 | tee "$build.build.log" | grep -E "error|undefined|FAILED|warning: implicit" | head -80 || true
    [ "${PIPESTATUS[0]}" -eq 0 ] || exit 1
    ls -l "$build/mksc98"
    ;;
  win98|profile)
    build="$here/build/$mode"; mkdir -p "$build" "$here/out"
    extra=""; [ "$mode" = profile ] && extra="-DMKSC98_PROFILE=ON"
    cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$here/toolchain-win98.cmake" $extra > "$build.configure.log" 2>&1 ||
        { tail -30 "$build.configure.log"; exit 1; }
    cmake --build "$build" --parallel "$(nproc)" 2>&1 | tee "$build.build.log" | grep -E "error|undefined|FAILED|warning: implicit" | head -80 || true
    [ "${PIPESTATUS[0]}" -eq 0 ] || exit 1
    name=mksc98.exe; [ "$mode" = profile ] && name=mksc98-profile.exe
    i686-w64-mingw32-strip -o "$here/out/$name" "$build/mksc98.exe"
    ls -l "$here/out/$name"
    bash "$here/check_exe.sh" "$build/mksc98.exe" "$here/exports"
    ;;
  *) echo "usage: build.sh [win98|host|profile]"; exit 2 ;;
esac
