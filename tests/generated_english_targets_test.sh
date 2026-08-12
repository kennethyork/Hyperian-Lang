#!/bin/sh
set -eu

workspace=$1
compiler=$2
mkdir -p "$workspace"

for target in web pwa console api service desktop mobile game; do
    project="English-$target"
    (
        cd "$workspace"
        "$compiler" new "$project" --target "$target"
    )
    source="$workspace/$project"
    grep 'field "item name" is text required' "$source/models/item.hyp" >/dev/null
    if find "$source" -name '*.hyp' -exec grep -H '^[[:space:]]*end[[:space:]]*$' {} + | grep . >/dev/null; then
        echo "generated $target project still contains end markers" >&2
        exit 1
    fi
    if find "$source" -name '*.hyp' -exec grep -H '^[[:space:]]\+' {} + | grep . >/dev/null; then
        echo "generated $target project is not fully left aligned" >&2
        exit 1
    fi
    grep '^that is all$' "$source/models/item.hyp" >/dev/null
    "$compiler" check "$source/app.hyp"
done

grep 'when input "item name" changes' "$workspace/English-desktop/controllers/items.hyp" >/dev/null
grep 'when input "item name" changes' "$workspace/English-mobile/controllers/items.hyp" >/dev/null
grep 'choose "Item kind" as "item kind"' "$workspace/English-desktop/views/main.hyp" >/dev/null
grep 'choose "Item kind" as "item kind"' "$workspace/English-mobile/views/main.hyp" >/dev/null
grep 'set "player left" to' "$workspace/English-game/controllers/items.hyp" >/dev/null
