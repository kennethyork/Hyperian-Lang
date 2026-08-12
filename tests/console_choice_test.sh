#!/bin/sh
set -eu

output=$($1 "$2")
printf '%s\n' "$output" | grep 'Priority: Low / Normal / High'
