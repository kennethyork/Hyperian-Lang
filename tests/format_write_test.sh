#!/bin/sh
set -eu

compiler=$1
source=$2
formatted=$3

cp "$source" "$formatted"
"$compiler" format "$formatted" --write

if grep '^[[:space:]]\+' "$formatted" >/dev/null; then
    echo "formatted source is not fully left aligned" >&2
    exit 1
fi

grep '^that is all$' "$formatted" >/dev/null
"$compiler" check "$formatted"
