#!/bin/bash
# Development aid: exact instruction counts per function (valgrind/callgrind)
# for the scripted route on the build host. Instruction ratios guide the
# optimisation; the 32-bit target adds its own cost to 64-bit arithmetic.
# Usage: dev_callgrind.sh "/path/to/rom.gba" [frames] [bench|hash]
here="$(cd "$(dirname "$0")" && pwd)"
b="$HOME/mksc-build/cg98"; mkdir -p "$b"
cmake -S "$here" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS='-g' > "$b.configure.log" 2>&1
cmake --build "$b" > "$b.build.log" 2>&1 || { grep -E "error" "$b.build.log" | head; exit 1; }
run="$HOME/mksc-run-cg"; mkdir -p "$run"; ln -sf "$1" "$run/Mario Kart - Super Circuit (USA).gba"
cd "$run" || exit 1
[ -f game.pack ] || MKSC98_DIR="$run" "$b/mksc98" --hash --frames 1 > /dev/null   # import outside the profile
rm -f callgrind.out.*
MKSC98_DIR="$run" valgrind --tool=callgrind --callgrind-out-file=callgrind.out.run -q \
    "$b/mksc98" "--${3:-hash}" --frames "${2:-900}" > /dev/null 2>&1
callgrind_annotate --inclusive=yes callgrind.out.run 2>/dev/null | grep -vE '^\s*$|^--|Profile|Events|Thresholds|Include|User|Auto|Collected|Trigger|Profiled|I1|D1|LL|Timerange' |
    sed -E "s#$(dirname "$here")/##; s# \[[^]]*\]\$##" | cut -c1-150 | head -"${TOP:-45}"
