#!/bin/bash
# ctest "emulator" for the cross-built tests: runs a Windows .exe from WSL,
# rewriting /mnt/<drive>/... arguments to <DRIVE>:/... so the program can
# open the fixture files it is given.
arguments=()
for argument in "$@"; do
    if [[ "$argument" =~ ^/mnt/([a-z])/(.*)$ ]]; then
        argument="${BASH_REMATCH[1]^^}:/${BASH_REMATCH[2]}"
    fi
    arguments+=("$argument")
done
exe="$1"; arguments=("${arguments[@]:1}")
exec "$exe" "${arguments[@]}"
