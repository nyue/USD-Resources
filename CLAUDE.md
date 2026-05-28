# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

USD-Resources is a collection of USD/Hydra plugins. Current component: **hairProc** — a Hydra 2.0 scene index plugin that deforms `BasisCurves` (groom) to follow an animated target mesh using OpenCL.

## Build

Two mutually exclusive build modes, controlled by CMake options:

```bash
# USD plugin (standalone OpenUSD install)
cmake -B build -S . \
  -DBUILD_USD_PLUGIN=ON \
  -DUSD_INSTALL_ROOT=/path/to/usd-install \
  -Dpeasyocl_DIR=/path/to/peasyocl

# Houdini plugin
cmake -B build -S . \
  -DBUILD_HOUDINI_PLUGIN=ON \
  -DHOUDINI_INSTALL_ROOT=/opt/hfsXX.X.XXX \
  -Dpeasyocl_DIR=/path/to/peasyocl

cmake --build build -j$(nproc)
cmake --install build --prefix /path/to/install
```

CMake presets are defined in `CMakePresets.json` (`ubuntu-debug`, `rh-debug`, `windows-debug`, `macos-debug`). All use Ninja.

Required external dependencies:
- `peasyocl` — OpenCL wrapper (find via `peasyocl_DIR`)
- `OpenGL` — required by hairProc
- `pxr` (USD) or Houdini — set via `USD_INSTALL_ROOT` / `HOUDINI_INSTALL_ROOT`

## Architecture

```
schema (HairProceduralAPI)
  └─ USD API schema applied to BasisCurves prims
  └─ attributes: hairProc:prim (int[]), hairProc:paramuv (float2[]),
                 hairProc:rest (float3[]), hairProc:target (rel)

imaging adapter (HairProceduralAPIAdapter)
  └─ bridges USD schema → Hydra 2.0 scene index

scene index (HairProceduralSceneIndex)
  └─ HdSingleInputFilteringSceneIndexBase
  └─ intercepts GetPrim() for BasisCurves with HairProceduralAPI applied
  └─ creates/owns one HairProceduralDeformer per deforming prim
  └─ registered via HairProceduralSceneIndexPlugin (plugInfo.json)

deformer (HairProceduralDeformer)
  └─ holds target + source HdContainerDataSourceHandles
  └─ Deform() dispatches to _DeformOCL() — GPU path via peasyocl
  └─ OpenCL kernel: hairProc/kernels/hairProc.cl
     - HairProc kernel: per-strand, frame-relative deformation
     - CalcTargetFrames kernel: builds 3x3 orthonormal frames per target prim
```

### Data flow at render time

1. `HairProceduralSceneIndex::GetPrim()` called for a BasisCurves path.
2. Scene index retrieves the deformer for that path and calls `Deform(shutterOffset)`.
3. Deformer uploads source/target point buffers to GPU, runs `CalcTargetFrames` then `HairProc` OpenCL kernels.
4. Returns modified `points` primvar replacing the original.

### Plugin registration

`plugInfo.json` registers three types:
- `HairProcHairProceduralAPI` — USD schema
- `HairProcHairProceduralAPIAdapter` — imaging adapter
- `HairProcHairProceduralSceneIndexPlugin` — scene index plugin (loads with all renderers, priority 1)

`plugInfoHoudini.json` is the Houdini variant; `create_target()` selects between them at configure time.

## CMake helper: `create_target()`

Defined in `cmake/createTarget.cmake`. Builds one shared library target (no `lib` prefix) plus an optional Python binding (`_<TARGET>.so` under `lib/python/<PYPACKAGE_NAME>/<ModuleName>/`).

Key behaviour:
- `BUILD_HOUDINI` flag switches `plugInfo.json` → `plugInfoHoudini.json` and adds `BUILD_HOUDINI_PLUGIN` compile definition.
- Python package name is `vik`; module name is capitalized target name (`HairProc`). Import: `from vik import HairProc`.

## Schema regeneration

If `schema.usda` is modified, regenerate the C++ boilerplate with:

```bash
usdGenSchema hairProc/usd/schema.usda hairProc/usd/
```

Files touched by codegen: `hairProceduralAPI.h/.cpp`, `tokens.h/.cpp`, `generatedSchema.usda`, `module.cpp`, `moduleDeps.cpp`, `wrapHairProceduralAPI.cpp`, `wrapTokens.cpp`.

## Windows notes (Houdini 20.5 + VS2022)

- **Build/install config**: MSVC is multi-config; `CMAKE_BUILD_TYPE` is ignored. Pass `--config RelWithDebInfo` (or `Release`) to **both** `cmake --build` AND `cmake --install` — mismatched configs silently install the wrong DLL.
- **`HoudiniThirdParty` INTERFACE target**: required on Windows for pxr/boost symbols (`Arch_ConstructorInit` etc. are unresolved without it). Wired up in `hairProc/usd/CMakeLists.txt` via `$<$<PLATFORM_ID:Windows>:HoudiniThirdParty>`.
- **`HAIRPROC_EXPORTS` define**: CMake auto-generates `hairProcHoudini_EXPORTS` but `api.h` checks `HAIRPROC_EXPORTS`. Without the explicit define on Windows, symbols get `dllimport` instead of `dllexport`.
- **`TF_REGISTRY_FUNCTION` does NOT fire on Windows for this plugin**: bodies of `TF_REGISTRY_FUNCTION(TfType)` and `TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)` in `hairProceduralSceneIndexPlugin.cpp` are never invoked at DLL load, even though the DLL loads and other static initializers in the same translation unit fire. The `#ifdef _WIN32` block at the bottom of that file performs `HdSceneIndexPluginRegistry::Define<>()` + `RegisterSceneIndexForRenderer()` directly from a plain static initializer, bypassing the TfRegistry mechanism. **Do not remove this block** when refactoring — Linux relies on TF_REGISTRY_FUNCTION and is unaffected.
- **`extern "C" __declspec(dllexport)` placement**: must be OUTSIDE `PXR_NAMESPACE_OPEN_SCOPE` — MSVC does not reliably emit it as a plain C symbol when declared inside a C++ namespace.
- **`PXR_PLUGINPATH_NAME` timing**: `houdini.env` is processed after `TfPlugRegistry` has scanned for plugins, so setting it there has no effect on Windows. Set as a Windows user env var (`setx` / `HKCU\Environment`) so it is present at process start.
- **Houdini user pref dir**: 20.5 on Windows uses `$USERPROFILE/houdini20.5/`, not `$USERPROFILE/Documents/houdini20.5/`. Check `dso.cache` timestamp to confirm which is active.

## Test data

`hairProc/testenv/genHairProc.py` generates a test USD stage (`hairProc.usda`) with tube + plane targets and 100k-strand grooms. Requires the `vik` Python package from the install tree.

```bash
PXR_PLUGINPATH_NAME=/path/to/install/lib/usd/hairProcHoudini/resources \
LD_LIBRARY_PATH=/path/to/install/lib \
PYTHONPATH=/path/to/install/lib/python \
OCL_KERNEL_PATHS=/path/to/install/ocl/kernels \
hython hairProc/testenv/genHairProc.py
```
