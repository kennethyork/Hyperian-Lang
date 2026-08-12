#!/bin/sh
set -eu

compiler=$1
source=$2
archive=$3
work=$4

platform=$("$compiler" platform)
mkdir "$work"

cp "$archive" "$work/extra-byte.hyr"
printf X >> "$work/extra-byte.hyr"
if "$compiler" build "$source" for "$platform" using "$work/extra-byte.hyr" as "$work/extra-byte-app"; then
    echo "expected an archive with trailing data to be rejected" >&2
    exit 1
fi
test ! -e "$work/extra-byte-app"

cp "$archive" "$work/changed-runtime.hyr"
size=$(wc -c < "$work/changed-runtime.hyr")
printf Z | dd of="$work/changed-runtime.hyr" bs=1 seek=$((size - 1)) conv=notrunc 2>/dev/null
if "$compiler" build "$source" for "$platform" using "$work/changed-runtime.hyr" as "$work/changed-runtime-app"; then
    echo "expected an archive with a changed runtime to be rejected" >&2
    exit 1
fi
test ! -e "$work/changed-runtime-app"

cp "$archive" "$work/bad-magic.hyr"
printf X | dd of="$work/bad-magic.hyr" bs=1 seek=0 conv=notrunc 2>/dev/null
if "$compiler" build "$source" for "$platform" using "$work/bad-magic.hyr" as "$work/bad-magic-app"; then
    echo "expected an archive with bad identity to be rejected" >&2
    exit 1
fi
test ! -e "$work/bad-magic-app"

ln -s "$archive" "$work/linked.hyr"
if "$compiler" build "$source" for "$platform" using "$work/linked.hyr" as "$work/linked-app"; then
    echo "expected a symbolic-link archive to be rejected" >&2
    exit 1
fi
test ! -e "$work/linked-app"
