#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-}}}"
if [ -z "$NDK_HOME" ]; then
    for SDK in "${ANDROID_SDK_ROOT:-}" "${ANDROID_HOME:-}" "$HOME/Android/Sdk" "$HOME/android-sdk"; do
        [ -n "$SDK" ] || continue
        if [ -d "$SDK/ndk" ]; then
            NDK_HOME="$(find "$SDK/ndk" -mindepth 1 -maxdepth 1 -type d -print | sort -V | tail -n 1)"
            [ -n "$NDK_HOME" ] && break
        fi
    done
fi
TOOLCHAIN="${NDK_HOME:+$NDK_HOME/build/cmake/android.toolchain.cmake}"
[ -f "${TOOLCHAIN:-}" ] || {
    echo "Android NDK not found. Set ANDROID_NDK_HOME or ANDROID_NDK_ROOT." >&2
    exit 1
}

if [ ! -f "$ROOT/resources/textures/animated_mojang/title/baked/title_atlas.json" ]; then
    python3 "$ROOT/tools/bake_title_atlas.py"
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-28 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel
"$ROOT/tools/make_levipack.sh" "$BUILD/libAnimatedMojang.so" "$ROOT/dist/animated_mojang.levipack"

mkdir -p /sdcard/Fonts
cp -f "$ROOT/dist/animated_mojang.levipack" /sdcard/Fonts/animated_mojang.levipack

echo "LeviPack: $ROOT/dist/animated_mojang.levipack"
