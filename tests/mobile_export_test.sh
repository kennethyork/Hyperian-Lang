#!/bin/sh
set -eu

package=$1
platform=$2

test -f "$package/application.hyc"
test -f "$package/hyperian.mobile"
test -f "$package/README.md"
grep 'format: HYMB1' "$package/hyperian.mobile"
grep "platform: $platform" "$package/hyperian.mobile"
grep 'target: mobile' "$package/hyperian.mobile"
grep 'bytecode: application.hyc' "$package/hyperian.mobile"
grep 'runtime interface: 1' "$package/hyperian.mobile"
grep 'English-like Hyperian source remains the application authority.' "$package/README.md"
grep "native mobile runtime library" "$package/README.md"

if [ "$platform" = android ]; then
    android="$package/android"
    test -f "$android/app/src/main/AndroidManifest.xml"
    test -f "$android/app/src/main/java/com/hyperian/generated/MainActivity.java"
    test -f "$android/app/src/main/cpp/hyperian_jni.c"
    test -f "$android/app/src/main/cpp/hyperian/mobile.c"
    test -f "$android/app/src/main/cpp/hyperian/runtime.c"
    cmp "$package/application.hyc" "$android/app/src/main/assets/application.hyc"
    grep 'id("com.android.application") version "9.3.0"' "$android/build.gradle.kts"
    grep 'externalNativeBuild' "$android/app/build.gradle.kts"
    grep 'add_library(hyperian_mobile_jni SHARED' "$android/app/src/main/cpp/CMakeLists.txt"
    grep 'hyperian_mobile_run_action' "$android/app/src/main/cpp/hyperian_jni.c"
    grep 'hyperian_mobile_send_event' "$android/app/src/main/cpp/hyperian_jni.c"
    grep 'System.loadLibrary("hyperian_mobile_jni")' "$android/app/src/main/java/com/hyperian/generated/MainActivity.java"
    grep 'private void scheduleTimer' "$android/app/src/main/java/com/hyperian/generated/MainActivity.java"
    grep '<string name="app_name">Mobile Tasks</string>' "$android/app/src/main/res/values/strings.xml"
fi
