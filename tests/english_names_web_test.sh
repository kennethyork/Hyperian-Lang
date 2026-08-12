#!/bin/sh
set -eu

compiler=$1
source=$2
workspace=$3
port=$((30000 + ($$ % 10000)))
log="$workspace/english-names-web-$$.log"
data="$workspace/english-names-web-$$.hdb"
page="$workspace/english-names-web-$$.html"

HYPERIAN_DATA="$data" "$compiler" run "$source" --port "$port" >"$log" 2>&1 &
server=$!
cleanup() {
    kill "$server" 2>/dev/null || true
    wait "$server" 2>/dev/null || true
    rm -f "$log" "$data" "$page"
}
trap cleanup EXIT INT TERM

attempt=0
until curl -fsS "http://127.0.0.1:$port/" >"$page"; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 50 ]; then
        cat "$log"
        exit 1
    fi
    sleep 0.1
done

grep 'Names can read like English.' "$page" >/dev/null
grep 'name="display title"' "$page" >/dev/null
curl -fsS -X POST -d 'display%20title=Readable+web+task' "http://127.0.0.1:$port/items" >/dev/null
curl -fsS "http://127.0.0.1:$port/" | grep 'Readable web task' >/dev/null
curl -fsS "http://127.0.0.1:$port/items" | grep '"display title":"Readable web task"' >/dev/null
