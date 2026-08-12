#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
wrong_output=$4
damaged_pack=$5
damaged_output=$6

platform=$("$compiler" platform)
if test "$platform" = linux-x64; then
    wanted=linux-arm64
else
    wanted=linux-x64
fi
if "$compiler" bundle "$source" for "$wanted" using "$pack" to "$wrong_output"; then
    echo "expected the wrong runtime pack to be rejected" >&2
    exit 1
fi
test ! -e "$wrong_output"

mkdir "$damaged_pack"
cp "$pack/runtime" "$damaged_pack/runtime"
cp "$pack/hyperian.runtime" "$damaged_pack/hyperian.runtime"
printf 'damaged' >> "$damaged_pack/runtime"
if "$compiler" bundle "$source" for "$platform" using "$damaged_pack" to "$damaged_output"; then
    echo "expected the changed runtime pack to be rejected" >&2
    exit 1
fi
test ! -e "$damaged_output"
