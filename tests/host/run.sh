#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export AM_ROOT="$(cd "$HERE/../.." && pwd)"
export LIBGL_ALWAYS_SOFTWARE=1
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
INC="-I$AM_ROOT/include -I$AM_ROOT/src"
if ldconfig -p 2>/dev/null | grep -q 'libEGL.so.1' && ldconfig -p 2>/dev/null | grep -q 'libGLESv2.so.2'; then
    GL="-l:libEGL.so.1 -l:libGLESv2.so.2 -lpthread"
elif [ -s /usr/lib/chromium/libEGL.so ] && [ -s /usr/lib/chromium/libGLESv2.so ]; then
    GL="-L/usr/lib/chromium -Wl,-rpath,/usr/lib/chromium -l:libEGL.so -l:libGLESv2.so -lpthread"
else
    echo "Missing EGL/GLES host runtime. Install Mesa EGL and GLESv2 runtime packages." >&2
    exit 1
fi
CXX="g++ -std=c++20 -O2 $INC"

$CXX "$HERE/test_cpu.cpp" "$AM_ROOT/src/scene/ImageDecode.cpp" -o "$OUT/cpu"
echo "== mesh builder / title atlas"
"$OUT/cpu"

$CXX "$HERE/test_robust.cpp" "$AM_ROOT/src/scene/CaveGlesRenderer.cpp" \
    "$AM_ROOT/src/scene/ImageDecode.cpp" $GL -o "$OUT/robust"
echo "== renderer robustness"
"$OUT/robust" | grep -E "PASS|FAIL|ALL|SOME"
