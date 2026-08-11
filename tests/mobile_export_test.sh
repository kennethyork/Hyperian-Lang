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
