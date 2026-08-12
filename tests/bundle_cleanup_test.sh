#!/bin/sh
set -eu

compiler=$1
source=$2
pack=$3
project=$4
bundle=$5

mkdir "$project"
cp "$source" "$project/app.hyp"
mkdir "$project/assets"
printf 'Hello from a bundled asset.\n' > "$project/assets/message.txt"
ln -s message.txt "$project/assets/linked-message.txt"
platform=$("$compiler" platform)
if "$compiler" bundle "$project/app.hyp" for "$platform" using "$pack" to "$bundle"; then
    echo "expected unsafe bundle assets to be rejected" >&2
    exit 1
fi
test ! -e "$bundle"
