#!/bin/sh
set -eu

project=$1
compiler=$2

test -f "$project/app.hyp"
test -f "$project/models/item.hyp"
test -f "$project/controllers/items.hyp"
test -f "$project/views/main.hyp"
grep 'field "item name" is text required' "$project/models/item.hyp" >/dev/null
grep 'collect every Item "item name" ordered by "item name" as "item names"' "$project/controllers/items.hyp" >/dev/null
grep 'when input "item name" changes' "$project/controllers/items.hyp" >/dev/null
grep 'joined with value called "item name"' "$project/controllers/items.hyp" >/dev/null
grep 'input "Your name" as "item name"' "$project/views/main.hyp" >/dev/null
grep 'for each "saved item" in "item names" show' "$project/views/main.hyp" >/dev/null
"$compiler" check "$project/app.hyp"
