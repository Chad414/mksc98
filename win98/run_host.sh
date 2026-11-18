#!/bin/bash
# Runs the headless reference build (./build.sh host) and prints its log.
# Usage: run_host.sh "/path/to/Mario Kart - Super Circuit (USA).gba" [options...]
# The ROM is linked into a private run directory outside the source tree,
# which also receives game.pack and the log.
rom="$1"; shift
run="$HOME/mksc-run"; mkdir -p "$run"
ln -sf "$rom" "$run/Mario Kart - Super Circuit (USA).gba"
MKSC98_DIR="$run" timeout "${TIMEOUT:-600}" "$HOME/mksc-build/host98/mksc98" "$@"
echo "exit status $?"
tail -n "${TAIL:-40}" "$run/mksc98.log"
