#!/bin/bash
# Post-build gate for the Win98 target. Usage: check_exe.sh FILE.exe [EXPORTS_DIR]
#  1. lists every DLL import of the final .exe;
#  2. if EXPORTS_DIR holds <dll>.exports lists taken from a real Win98 SE
#     install (see README.md, Building), fails on any import missing from them;
#  3. disassembles the .exe and fails on any SSE/SSE2/AVX instruction, since
#     neither the VM nor the build host will trap them.
set -u
exe="$1"; exports="${2:-}"
objdump=i686-w64-mingw32-objdump
status=0

echo "== imports of $(basename "$exe")"
"$objdump" -p "$exe" | awk '
  /DLL Name:/ { dll=tolower($3); next }
  /^\t[0-9a-f]+\t *[0-9]+ +[A-Za-z_@?]/ { print dll, $NF }
  /^\t[0-9a-f]+  *<none>  *[0-9a-f]+  *[A-Za-z_@?]/ { print dll, $NF }
' | sort -u > "$exe.imports"
cut -d' ' -f1 "$exe.imports" | sort | uniq -c | awk '{printf "  %-14s %s symbols\n", $2, $1}'

if [ -n "$exports" ] && [ -d "$exports" ]; then
  echo "== imports checked against Win98 SE export lists in $exports"
  while read -r dll symbol; do
    list="$exports/$dll.exports"
    if [ ! -f "$list" ]; then echo "  NO EXPORT LIST for $dll"; status=1; continue; fi
    tr -d '\r' < "$list" | grep -qxF "$symbol" || { echo "  MISSING on Win98: $dll!$symbol"; status=1; }
  done < "$exe.imports"
  [ $status -eq 0 ] && echo "  all imports present"
else
  echo "== (no Win98 export lists given; imports not verified against the guest)"
fi

echo "== SSE/AVX scan"
"$objdump" -d --no-show-raw-insn "$exe" | awk '
  /^[0-9a-f]+ <.*>:$/ { fn=$2; next }
  /%[xyz]mm[0-9]/ ||
  /[ \t](ldmxcsr|stmxcsr|fxsave|fxrstor|sfence|lfence|mfence|movnti|movntq|maskmovq|pshufw|pavgb|pavgw|pmaxub|pminub|pmaxsw|pminsw|pmulhuw|psadbw|pextrw|pinsrw|pmovmskb|prefetchnta|prefetcht[012]|pause|clflush)[ \t]/ {
      count[fn]++; if (shown[fn]++ < 2) print "  " fn " " $0
  }
  END { total=0; for (f in count) { total+=count[f]; n++ }
        printf "  %d flagged instruction(s) in %d function(s)\n", total, n+0
        exit total ? 1 : 0 }
' || status=1
exit $status
