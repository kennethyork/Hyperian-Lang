#!/bin/sh
set -eu

hyperian=$1
source=$2
output=$3
keystore=$4
builder=$5

printf 'keep this file\n' > "$output"
if HYPERIAN_APPLICATION_ID=com.example.mobiletasks \
   HYPERIAN_ANDROID_KEYSTORE="$keystore" \
   HYPERIAN_ANDROID_KEY_ALIAS=release \
   HYPERIAN_ANDROID_STORE_PASSWORD=store-secret \
   HYPERIAN_ANDROID_KEY_PASSWORD=key-secret \
   HYPERIAN_GRADLE="$builder" \
   HYPERIAN_FAKE_PLATFORM=android-apk \
   "$hyperian" build "$source" for android as "$output"; then
    printf 'mobile build unexpectedly overwrote an existing output\n' >&2
    exit 1
fi
grep 'keep this file' "$output"
