#!/bin/bash
# Development aid: dump composed pictures from the reference build at several
# moments of a scripted route. Usage:
#   dev_tall.sh ROM NAME "FRAME FRAME ..." [extra options...]
# Pictures land in out/tall/NAME-FRAME.png (3x).
w="$(cd "$(dirname "$0")" && pwd)"
rom="$1"; name="$2"; frames="$3"; shift 3
mkdir -p "$w/out/tall"
for n in $frames; do
  TAIL=4000 bash "$w/run_host.sh" "$rom" --hash --frames "$n" --dump-frame "$w/out/tall/$name-$n.bmp" "$@" > /tmp/dev_tall.log
  grep -E '^tall:|^aspect:|rror|fail|exit: [^0]' /tmp/dev_tall.log | sort -u | cut -c1-160
  python3 "$w/bmp2png.py" "$w/out/tall/$name-$n.bmp" "$w/out/tall/$name-$n.png" 3 && rm -f "$w/out/tall/$name-$n.bmp"
done
ls "$w/out/tall" | grep "^$name-"
