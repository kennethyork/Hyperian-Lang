#!/bin/sh
set -eu

if [ -n "${HYPERIAN_FAKE_LOG:-}" ]; then
    printf '%s\n' "$*" >> "$HYPERIAN_FAKE_LOG"
fi

case "${HYPERIAN_FAKE_PLATFORM:-}" in
    android-apk)
        mkdir -p app/build/outputs/apk/release
        printf 'signed android apk\n' > app/build/outputs/apk/release/app-release.apk
        ;;
    android-aab)
        mkdir -p app/build/outputs/bundle/release
        printf 'signed android bundle\n' > app/build/outputs/bundle/release/app-release.aab
        ;;
    ios)
        export_path=""
        archive_path=""
        options_path=""
        previous=""
        for argument in "$@"; do
            if [ "$previous" = export ]; then export_path=$argument; fi
            if [ "$previous" = archive ]; then archive_path=$argument; fi
            if [ "$previous" = options ]; then options_path=$argument; fi
            if [ "$argument" = -exportPath ]; then previous=export
            elif [ "$argument" = -archivePath ]; then previous=archive
            elif [ "$argument" = -exportOptionsPlist ]; then previous=options
            else previous=""
            fi
        done
        if [ -n "$options_path" ]; then
            grep '<string>app-store-connect</string>' "$options_path" >/dev/null
            grep '<string>ABCDE12345</string>' "$options_path" >/dev/null
        fi
        if [ -n "$archive_path" ]; then mkdir -p "$archive_path"; fi
        if [ -n "$export_path" ]; then
            mkdir -p "$export_path"
            printf 'signed ios archive\n' > "$export_path/HyperianIOS.ipa"
        fi
        ;;
    *)
        printf 'fake mobile builder needs HYPERIAN_FAKE_PLATFORM\n' >&2
        exit 2
        ;;
esac
