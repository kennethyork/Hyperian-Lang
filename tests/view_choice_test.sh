#!/bin/sh
set -eu

compiler=$1
source=$2
output=$3

"$compiler" compile "$source" -o "$output"
inspection=$($compiler inspect "$output")
printf '%s\n' "$inspection" | grep ' CHOICE '
printf '%s\n' "$inspection" | grep ' CHOICE_OPTION '
printf '%s\n' "$inspection" | grep ' END_CHOICE '
format=$($compiler format "$source")
printf '%s\n' "$format" | grep '^choose "Priority" as priority required$'
printf '%s\n' "$format" | grep '^offer "Normal" as normal$'
