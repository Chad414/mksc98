#!/bin/bash
# Development aid: build a variant of the Win98 exe with extra cmake options.
# Usage: dev_variant.sh NAME [cmake options...]  -> out/mksc98-NAME.exe
here="$(cd "$(dirname "$0")" && pwd)"; name="$1"; shift
b="$here/build/variant-$name"; mkdir -p "$b"
cmake -S "$here" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$here/toolchain-win98.cmake" "$@" > "$b.configure.log" 2>&1 || { tail -20 "$b.configure.log"; exit 1; }
cmake --build "$b" > "$b.build.log" 2>&1 || { grep -E "error|undefined" "$b.build.log" | head -20; exit 1; }
i686-w64-mingw32-strip -o "$here/out/mksc98-$name.exe" "$b/mksc98.exe" && ls -l "$here/out/mksc98-$name.exe"