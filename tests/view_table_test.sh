#!/bin/sh
set -eu

compiler=$1
source=$2
output=$3

"$compiler" compile "$source" -o "$output"
inspection=$($compiler inspect "$output")
printf '%s\n' "$inspection" | grep ' VIEW_TABLE '
printf '%s\n' "$inspection" | grep ' END_VIEW_TABLE '
printf '%s\n' "$inspection" | grep ' TABLE_HEADING '
printf '%s\n' "$inspection" | grep ' TABLE_ROW '
printf '%s\n' "$inspection" | grep ' END_TABLE_ROW '
printf '%s\n' "$inspection" | grep ' TABLE_CELL '
format=$($compiler format "$source")
printf '%s\n' "$format" | grep '^show the following in a table$'
printf '%s\n' "$format" | grep '^use "Task" as a table heading$'
printf '%s\n' "$format" | grep '^show the following in a table row$'
printf '%s\n' "$format" | grep '^show task_title in a table cell$'
