#!/bin/sh
set -eu

compiler=$1
source=$2
workspace=$3
port=$((20000 + ($$ % 20000)))
log="$workspace/pwa-runtime-$$.log"
data="$workspace/pwa-runtime-$$.hdb"
home="$workspace/pwa-home-$$.html"
headers="$workspace/pwa-headers-$$.txt"

HYPERIAN_DATA="$data" "$compiler" run "$source" --port "$port" >"$log" 2>&1 &
server=$!
cleanup() {
    kill "$server" 2>/dev/null || true
    wait "$server" 2>/dev/null || true
    rm -f "$log" "$data" "$home" "$headers"
}
trap cleanup EXIT INT TERM

attempt=0
until curl -fsS "http://127.0.0.1:$port/" >"$home"; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 50 ]; then
        cat "$log"
        exit 1
    fi
    sleep 0.1
done

grep 'rel="manifest" href="/assets/manifest.webmanifest"' "$home"
grep "serviceWorker.register('/service-worker.js')" "$home"
curl -fsS -D "$headers" "http://127.0.0.1:$port/assets/manifest.webmanifest" | grep '"display": "standalone"'
grep -i 'content-type: application/manifest+json' "$headers"
curl -fsS "http://127.0.0.1:$port/service-worker.js" | grep 'hyperian-pocket-tasks-v1'
