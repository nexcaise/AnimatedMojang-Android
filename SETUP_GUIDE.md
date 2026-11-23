# Setup and development guide

## 1. Target requirements

- Minecraft Bedrock Android with an `arm64-v8a` process.
- Android API 28 or newer.
- A loader that can load a native mod exposing `PLGetModRegistration` or `JNI_OnLoad`.

The runtime is not pinned to Minecraft 1.26.52 or any other exact game version. It checks for known function signatures in the loaded `libminecraftpe.so`; only successfully resolved functions receive hooks. Newer versions, including 1.26.60+, can be supported by adding verified patterns to `src/client/Signatures.h` when Mojang changes the machine code.

## 2. Install

1. Build or obtain `animated_mojang.levipack`.
2. Import it through the mod import page in LeviLauncher/LeviLaunchroid.
3. Ensure `libAnimatedMojang.so` is present in the mod directory after extraction. `libnmdk.so` is not in the package; NMDK must be provided by your launcher/loader.
4. Enable the mod and launch the target Minecraft build.
5. Check the mod log for `Installing NMDK hooks using executable signatures` and inspect any `NMDK signature not found` warnings.

A missing signature means the function's current machine code does not match the patterns bundled with this source. Do not replace it with an unverified RVA. Extract a pattern from the exact target binary, verify its uniqueness and function boundary, then add it to the corresponding array in `src/client/Signatures.h`.

## 3. Build from source

The NMDK arm64-v8a headers and runtime are vendored in `vendor/nmdk/`.

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

`package_mod.sh` wraps the configure/build/package steps for an environment where the Android NDK is available through CMake. Do not use a host x86-64 compiler to build the mod itself.

## 4. Host tests

```bash
tests/host/run.sh
```

These tests exercise CPU-side mesh/atlas handling and renderer robustness. They do not validate Minecraft function signatures, NMDK trampolines, loader integration, or runtime behaviour inside Minecraft.

## 5. Hook integration notes

- `nmdk::library::base("libminecraftpe.so")` resolves the loaded game module.
- `nmdk::signature::resolveSignatures()` resolves all patterns declared in the single header `src/client/Signatures.h` in one API call; the header selects the first matching variant for each function.
- All inline hook patterns and graphics GOT symbol names are centralized in that header.
- `nmdk::hook::hook()` installs inline hooks; `nmdk::hook::hookGOT()` installs graphics symbol hooks.
- The AArch64 architecture and loaded-module checks are retained. There is no version string, GNU Build ID allowlist, or hardcoded game RVA check.
- If a function signature is missing, that hook is skipped while other hooks are still attempted.
- Hook installation is one-shot per process. Disable/enable does not chain duplicate trampolines.

When supporting a new game version, append a verified pattern to the appropriate array in `Signatures.h`. No version-specific offset table or edits to `Client.cpp` should be necessary. Verify against that version's actual `libminecraftpe.so` that each pattern resolves exactly once and points to the intended function entry point before release.

## 6. Package contents

The LeviPack includes only runtime assets and the mod library:

- `libAnimatedMojang.so`
- `manifest.json`, `icon.png`
- `config/config.json`
- `resources/data/` and the baked title atlas

The source-only title frame PNGs are removed from the final LeviPack because the baked atlas is what the renderer loads.
