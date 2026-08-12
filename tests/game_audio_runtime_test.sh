#!/bin/sh
set -eu

compiler=$1
ffmpeg=$2
source=$3
bytecode=$4

if ! "$compiler" doctor | grep -F "compressed effects and streaming music (SDL2_mixer): ready" >/dev/null; then
    echo "SDL2_mixer is not available; skipping compressed audio runtime test."
    exit 77
fi

mkdir -p assets
"$ffmpeg" -hide_banner -loglevel error -f lavfi -i "anullsrc=r=8000:cl=mono" -t 0.02 -c:a libvorbis -q:a 0 -y assets/effect.ogg
cp assets/effect.ogg assets/theme.ogg
"$compiler" compile "$source" -o "$bytecode"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HYPERIAN_VISUAL_TEST=1 HYPERIAN_VISUAL_TEST_STATE="audio ready" "$compiler" run "$bytecode"
