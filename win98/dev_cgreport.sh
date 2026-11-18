#!/bin/bash
# Development aid: print the function tables of the last dev_callgrind.sh run.
# Usage: dev_cgreport.sh [inclusive|self] [count]
run="$HOME/mksc-run-cg"
root="$(cd "$(dirname "$0")/.." && pwd)"
mode=yes; [ "${1:-inclusive}" = self ] && mode=no
callgrind_annotate --auto=no --inclusive=$mode --threshold=99.9 "$run/callgrind.out.run" 2>/dev/null |
    grep -E '^ *[0-9,]+ \(' | grep -vE 'libc|ld-linux|below main|\?\?\?:0x' |
    sed -E "s#$root/##; s# \[[^]]*\]\$##" | cut -c1-140 | head -"${2:-40}"
