#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOD_SO="${1:?usage: make_levipack.sh <libAnimatedMojang.so> [out.levipack]}"
OUT="${2:-$ROOT/dist/animated_mojang.levipack}"
STAGE="$ROOT/dist/AnimatedMojang"

[ -s "$MOD_SO" ] || { echo "Missing mod library: $MOD_SO" >&2; exit 1; }
[ -s "$ROOT/resources/textures/animated_mojang/title/baked/title_atlas.json" ] || {
    echo "Missing baked title atlas. Run tools/bake_title_atlas.py first." >&2
    exit 1
}

rm -rf "$ROOT/dist"
mkdir -p "$STAGE" "$(dirname "$OUT")"
cp "$MOD_SO" "$STAGE/libAnimatedMojang.so"
cp "$ROOT/manifest.json" "$ROOT/icon.png" "$STAGE/"
cp -R "$ROOT/config" "$STAGE/"
cp -R "$ROOT/resources" "$STAGE/"
rm -rf "$STAGE/resources/textures/animated_mojang/title/minecraft_animation"
rm -f "$STAGE/resources/data/normal_cave.particles" "$STAGE/resources/data/normal_cave.schem"
rm -f "$STAGE/resources/textures/animated_mojang/cave/blocks.png" \
      "$STAGE/resources/textures/animated_mojang/cave/lava.png" \
      "$STAGE/resources/textures/animated_mojang/cave/loading_screen-100.png" \
      "$STAGE/resources/textures/animated_mojang/cave/particles.png"

python3 - "$STAGE" "$OUT" <<'PY'
import os, sys, zipfile
stage, destination = sys.argv[1:3]
with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for base, _, files in os.walk(stage):
        for name in sorted(files):
            path = os.path.join(base, name)
            archive.write(path, os.path.relpath(path, stage))
print(f"Created {destination} ({os.path.getsize(destination) / 1048576:.2f} MiB)")

PY
