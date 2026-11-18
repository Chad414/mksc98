#!/bin/bash
# Development aid: compile one target of the host build and summarize errors.
# Usage: dev_errors.sh [target] [win98]
here="$(cd "$(dirname "$0")" && pwd)"
target="${1:-mksc98}"
if [ "${2:-host}" = win98 ]; then
    b="$here/build/win98"; tc="-DCMAKE_TOOLCHAIN_FILE=$here/toolchain-win98.cmake"
else
    b="$HOME/mksc-build/host98"; tc=""
fi
mkdir -p "$b"
cmake -S "$here" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release $tc > "$b.configure.log" 2>&1 || { tail -20 "$b.configure.log"; exit 1; }
cmake --build "$b" --target "$target" -- -k 0 > "$b.build.log" 2>&1
status=$?
grep -E 'error|undefined reference|multiple definition' "$b.build.log" |
    sed -E "s#$(dirname "$here")/##" | sed -E 's#^.*ld: ##; s#^.*\.o(bj)?:[^:]*: ##; s#^.*\.c:\(\.text[^)]*\): ##' |
    cut -c1-240 | sort | uniq -c | sort -rn | head -"${LINES_MAX:-80}"
echo "status=$status errors=$(grep -cE 'error|undefined reference' "$b.build.log")"
