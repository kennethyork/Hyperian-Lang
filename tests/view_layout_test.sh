#!/bin/sh
set -eu

compiler=$1
source=$2
output=$3

"$compiler" compile "$source" -o "$output"
inspection=$($compiler inspect "$output")
printf '%s\n' "$inspection" | grep VIEW_ROW
printf '%s\n' "$inspection" | grep END_VIEW_ROW
printf '%s\n' "$inspection" | grep VIEW_COLUMN
printf '%s\n' "$inspection" | grep END_VIEW_COLUMN
printf '%s\n' "$inspection" | grep VIEW_CARD
printf '%s\n' "$inspection" | grep END_VIEW_CARD
format=$($compiler format "$source")
printf '%s\n' "$format" | grep '^arrange the following in a row$'
printf '%s\n' "$format" | grep '^arrange the following in a column$'
printf '%s\n' "$format" | grep '^show the following in a card$'
