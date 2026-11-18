#!/bin/bash
# Development aid: gprof profile of the scripted race on the build host.
# Usage: dev_profile.sh "/path/to/rom.gba" [frames]
# Shows where host CPU time goes; ratios carry over to the target roughly,
# absolute times do not.
here="$(cd "$(dirname "$0")" && pwd)"
b="$HOME/mksc-build/prof98"; mkdir -p "$b"
cmake -S "$here" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS='-pg -fno-inline-functions -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS=-pg > "$b.configure.log" 2>&1
cmake --build "$b" > "$b.build.log" 2>&1 || { grep -E "error" "$b.build.log" | head; exit 1; }
run="$HOME/mksc-run-prof"; mkdir -p "$run"; ln -sf "$1" "$run/Mario Kart - Super Circuit (USA).gba"
cd "$run" && MKSC98_DIR="$run" "$b/mksc98" --bench --frames "${2:-3000}" > /dev/null
grep bench "$run/mksc98.log"
gprof -b -p "$b/mksc98" "$run/gmon.out" 2>/dev/null | head -"${TOP:-32}"
