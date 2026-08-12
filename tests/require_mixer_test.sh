#!/bin/sh
set -eu

compiler=$1
shift
if ! "$compiler" doctor | grep -F "compressed effects and streaming music (SDL2_mixer): ready" >/dev/null; then
    echo "SDL2_mixer is not available; skipping mixer-specific test."
    exit 77
fi
exec "$@"
