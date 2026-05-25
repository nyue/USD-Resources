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

## Test data

`hairProc/testenv/genHairProc.py` generates a test USD stage (`hairProc.usda`) with tube + plane targets and 100k-strand grooms. Requires the `vik` Python package from the install tree.

```bash
PYTHONPATH=/path/to/install/lib/python python hairProc/testenv/genHairProc.py
```
