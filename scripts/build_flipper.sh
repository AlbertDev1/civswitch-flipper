#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
ufbt
test -s dist/civswitch_flipper.fap
printf 'Built %s\n' "$(pwd)/dist/civswitch_flipper.fap"
