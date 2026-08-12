#!/bin/sh
set -eu

package=$1
compiler=$2
source=$3
version=$4

test -f "$package/README.md"
test -f "$package/RUNNING.md"
test -x "$compiler"
"$compiler" version | grep "Hyperian $version"
"$compiler" help run | grep 'You do not need a .hyr file'
"$compiler" "$source" | grep 'Hello from left aligned Hyperian'
