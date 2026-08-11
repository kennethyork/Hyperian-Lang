#!/bin/sh
set -eu

artifact=$1
platform=$2
log=$3

test -s "$artifact"
test -s "$log"

case "$platform" in
    apk)
        grep -- '--no-daemon :app:assembleRelease' "$log"
        grep 'signed android apk' "$artifact"
        ;;
    aab)
        grep -- '--no-daemon :app:bundleRelease' "$log"
        grep 'signed android bundle' "$artifact"
        ;;
    ipa)
        grep -- '-project HyperianIOS.xcodeproj -scheme HyperianIOS' "$log"
        grep -- '-exportArchive' "$log"
        grep 'DEVELOPMENT_TEAM=ABCDE12345' "$log"
        grep 'PRODUCT_BUNDLE_IDENTIFIER=com.example.mobiletasks' "$log"
        grep 'signed ios archive' "$artifact"
        ;;
esac

if grep -E 'store-secret|key-secret' "$log"; then
    printf 'a signing password leaked into mobile builder arguments\n' >&2
    exit 1
fi
