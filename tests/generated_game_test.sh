#!/bin/sh
set -eu

project=$1
compiler=$2

test -f "$project/app.hyp"
test -f "$project/models/item.hyp"
test -f "$project/controllers/items.hyp"
test -f "$project/views/main.hyp"
grep 'field "item name" is text required' "$project/models/item.hyp" >/dev/null
grep 'set "player left" to' "$project/controllers/items.hyp" >/dev/null
grep 'check whether rectangle .* touches circle centered at' "$project/controllers/items.hyp" >/dev/null
grep 'draw circle centered at' "$project/views/main.hyp" >/dev/null
grep 'check whether line from .* touches circle centered at' "$project/controllers/items.hyp" >/dev/null
grep 'draw line from' "$project/views/main.hyp" >/dev/null
"$compiler" check "$project/app.hyp"
