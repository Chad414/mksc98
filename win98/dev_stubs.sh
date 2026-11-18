#!/bin/bash
# Development aid: regenerate stubs.c from the current set of undefined
# references. Empties stubs.c, links the host build to collect them, then
# runs gen_stubs.py and links again.
here="$(cd "$(dirname "$0")" && pwd)"
b="$here/build/win98"   # the PE link keeps more code alive than ELF: use it
: > "$here/stubs.c"
bash "$here/dev_errors.sh" mksc98 win98 > /dev/null
python3 "$here/gen_stubs.py" "$b.build.log"
LINES_MAX="${LINES_MAX:-60}" bash "$here/dev_errors.sh" mksc98 win98
