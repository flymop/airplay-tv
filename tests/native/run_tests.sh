#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/airplay-audio-tests.XXXXXX")"
trap 'rm -rf "$test_dir"' EXIT

# Match the SDK to the selected Xcode toolchain on macOS.
if [[ "$(uname -s)" == Darwin ]]; then
    export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
fi

for test_name in timeline_buffer_test audio_engine_test; do
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
        -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
        -I "$repo_root/app/src/main/cpp" \
        "$repo_root/tests/native/$test_name.cpp" \
        -o "$test_dir/$test_name"
    "$test_dir/$test_name"
done

# Build the pinned upstream queue with the shipped patches, even before Gradle's
# patch task has run. Avoid depending on the developer's modified submodule tree.
uxplay_root="$repo_root/app/src/main/cpp/third_party/UxPlay"
mkdir -p "$test_dir/uxplay"
git -C "$uxplay_root" archive HEAD lib/raop_buffer.c lib/raop_buffer.h lib/raop_rtp.c |
    tar -x -C "$test_dir/uxplay"
for patch_name in 0008-flush-audio-receive-buffer 0009-bound-mirroring-audio-resends; do
    patch -s -d "$test_dir/uxplay" -p1 < \
        "$repo_root/app/src/main/cpp/patches/UxPlay/$patch_name.patch"
done
"${CC:-cc}" -std=c11 -O1 -Wall -Wextra -Werror \
    -fsanitize="${SANITIZERS:-undefined}" -fno-omit-frame-pointer \
    -I "$test_dir/uxplay/lib" -I "$uxplay_root/lib" \
    "$repo_root/tests/native/raop_buffer_test.c" "$uxplay_root/lib/byteutils.c" \
    -o "$test_dir/raop_buffer_test"
"$test_dir/raop_buffer_test"
