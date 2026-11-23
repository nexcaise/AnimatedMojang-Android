# Animated Mojang — NMDK Edition

Native Android ARM64 port of Animated Mojang, using **NMDK** for inline and GOT hooks.

## Features

- OpenGL ES 3 cave scene and animated Minecraft title.
- Camera transitions for Start Menu sub-pages.
- Main-menu-only cave rendering (paused in-world); the replacement logo also shows on pause_screen and the world saving/loading progress screens.
- Progressive title texture upload and GPU resource eviction.
- EGL context-loss recovery and render-thread GL resource cleanup.
- NMDK signature-scanned inline hooks and NMDK GOT hooks for graphics entry points.
- No resource pack required.

## Compatibility

| Item | Supported configuration |
|---|---|
| Minecraft Bedrock | No exact-version gate; each native hook requires a matching signature |
| Android | API 28 or newer |
| ABI | `arm64-v8a` |
| Graphics | OpenGL ES 3.x / EGL |
| Hook framework | NMDK (bundled) |
| Package | LeviPack |

The runtime does not compare a Minecraft version string or GNU Build ID, and it has no RVA allowlist. Virtual functions (eleven targets) are located with NMDK's virtual functions Resolver by one fixed class vtable symbol + slot index; the other twelve use one fixed signature each. There are no fallback candidates: all values target Minecraft 1.26.62 (build 2662). NMDK's `resolveSignatures()` resolves all declared patterns against the executable region of `libminecraftpe.so` in one API call. Each function has exactly one pattern or one vtable slot; if it does not resolve, that individual hook is skipped (the reason is logged) while the remaining hooks are still attempted. No version gate rejects a newer build, but the fixed values target 1.26.62 only.

Patterns and vtable slots were checked against Minecraft 1.26.52 and 1.26.62 `libminecraftpe.so` builds (static analysis only, not an on-device run). Compatibility remains **signature/slot-dependent, not version-guaranteed**. A build such as 1.26.60+ can use the existing patterns when the relevant machine code still matches. When Mojang changes a function, add a newly derived and verified pattern to that function's array in `src/client/Signatures.h`; no per-version RVA table or edit in `Client.cpp` is needed.

## Project layout

```text
.
├── src/
│   ├── animation/       # Presentation timeline
│   ├── client/          # Lifecycle, hooks and centralized signatures
│   │   └── Signatures.h # All inline patterns and GOT symbol names
│   ├── mod/             # Mod configuration and loader registration
│   ├── runtime/         # Minimal Android/host logger
│   └── scene/           # Cave camera, mesh, atlas and GLES renderer
├── include/             # Used header-only dependencies and GLES/EGL headers
├── vendor/nmdk/         # NMDK headers and arm64-v8a runtime
├── resources/           # Cave data and title textures
├── config/              # Default mod configuration
├── tests/host/          # Host-side renderer and mesh tests
└── tools/               # LeviPack and title-atlas tools
```

## Build

Requirements: Android NDK with CMake toolchain, CMake 3.22+, Ninja, Python 3. The supplied NMDK binary is arm64-v8a, so other ABIs are rejected.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
tools/make_levipack.sh build/libAnimatedMojang.so dist/animated_mojang.levipack
```

Or run `./package_mod.sh` in an Android NDK environment. The package contains `libAnimatedMojang.so`, `manifest.json`, `config/`, `resources/`, and `icon.png`. It does **not** contain `libnmdk.so`: the mod links against NMDK at build time only, and the runtime (launcher/loader) must already provide `libnmdk.so`.

## Host tests

The host suite validates CPU-side mesh/atlas processing and renderer robustness. It does **not** emulate Minecraft, validate the game's function signatures, test NMDK trampolines, or replace an on-device test.

```bash
tests/host/run.sh
```

## Runtime configuration

The active configuration is `config/config.json`:

| Key | Default | Description |
|---|---:|---|
| `enabled` | `true` | Master toggle |
| `minecraftTitleAnimation` | `true` | Enable the animated title |
| `caveBackground` | `true` | Enable the 3D cave scene |
| `replayOnReturnToMenu` | `false` | Replay the intro after leaving a world |
| `blockMenuInputDuringIntro` | `true` | Prevent menu input during the intro |
| `maxPresentationSeconds` | `15.0` | Maximum intro presentation duration |
| `menuScreenNames` | Start-screen names | Additional menu route names |

## Hook policy

- `nmdk::library::base()` obtains the loaded `libminecraftpe.so` base address.
- `nmdk::signature::resolveSignatures()` resolves all patterns declared in `src/client/Signatures.h` in one API call; `firstMatch()` selects the first matching variant per function.
- `nmdk::hook::hook()` installs resolved inline hooks.
- `nmdk::hook::hookGOT()` installs the `glClear`, `glColorMask`, and `eglSwapBuffers` relocation hooks.
- AArch64 and module-loaded checks remain mandatory. There is no exact Minecraft version or Build ID gate.
- A missing signature skips only that hook; other hooks are still attempted.
- Hook installation is one-shot per process. Disable/enable does not chain duplicate trampolines.

### Adding a newer Minecraft version

1. Test the existing patterns against that version's `libminecraftpe.so`.
2. For each function whose pattern no longer resolves, derive and verify a new AArch64 pattern from that exact binary.
3. Replace the signature or virtual functions index in `src/client/Signatures.h` with the value verified for that binary. Check the log for `Hook FAILED [...]` lines to see exactly which targets failed and why.
4. Run the host tests, build for Android, and validate every resolved hook on the target device.

Do not add guessed patterns or reuse stale RVAs. The resolver returning an address is not, by itself, proof that the match is semantically correct; verify matches are unique and begin at the intended function before publishing.

## Attribution and licences

- Android ARM64 port and integration: NexCaise Team.
- Original Windows x64 Animated Mojang project: MRUIAW.
- Original Java Edition concept/assets: chara2015.
- See `LICENSE`, `COPYING.txt`, `COPYING.LESSER`, and `NOTICE.md` for licence and attribution details.
