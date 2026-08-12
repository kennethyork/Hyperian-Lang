#!/bin/sh
set -eu

output=$($1 run "$2")
printf '%s\n' "$output" | grep 'Task report'
printf '%s\n' "$output" | grep 'Task'
printf '%s\n' "$output" | grep 'Write the model'
printf '%s\n' "$output" | grep 'Render the view'
if printf '%s\n' "$output" | grep -- '- Write the model' >/dev/null; then
    echo "console table rows must not use list bullets" >&2
    exit 1
fi
