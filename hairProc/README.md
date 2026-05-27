# hairProc — Hydra 2.0 Hair Procedural

A Hydra 2.0 scene index plugin that deforms `BasisCurves` (groom) to follow an
animated target mesh using OpenCL. Supports both standalone OpenUSD and Houdini
(OpenGL viewport and Karma CPU renderer).

## How it works

At stage load time, strands are _captured_ against a rest-pose mesh — each strand
root is assigned a face index (`hairProc:prim`), a parametric UV on that face
(`hairProc:paramuv`), and a rest-frame position (`hairProc:rest`). At render time
the plugin:

1. Builds an orthonormal frame per unique captured face on the animated mesh
   (`CalcTargetFrames` OpenCL kernel).
2. Applies the frame-relative offset from rest to each strand point
   (`HairProc` OpenCL kernel).
3. Returns the deformed `points` primvar, replacing the original.

## Architecture

```
schema (HairProceduralAPI)
  └─ USD API schema applied to BasisCurves prims
  └─ attributes: hairProc:prim (int[]), hairProc:paramuv (float2[]),
                 hairProc:rest (float3[]), hairProc:target (rel)

imaging adapter (HairProceduralAPIAdapter)
  └─ bridges USD schema -> Hydra 2.0 scene index

scene index (HairProceduralSceneIndex)
  └─ HdSingleInputFilteringSceneIndexBase
  └─ intercepts GetPrim() for BasisCurves that have a registered deformer
  └─ creates/owns one HairProceduralDeformer per deforming prim
  └─ registered via HairProceduralSceneIndexPlugin (plugInfo.json)

deformer (HairProceduralDeformer)
  └─ holds target + source HdContainerDataSourceHandles
  └─ Deform() dispatches to _DeformOCL() — GPU path via peasyocl
  └─ OpenCL kernels: hairProc/kernels/hairProc.cl
     - CalcTargetFrames: builds a 3x3 orthonormal frame per captured face
     - HairProc: per-strand, frame-relative deformation
```

### Thread safety

The peasyocl `Context` is a singleton with a single `cl::CommandQueue`. Karma
CPU invokes `GetPrim()` from multiple worker threads concurrently, so all
`_DeformOCL` and `_CalcTargetFrames` calls are serialised with a process-wide
`std::mutex`.

### Houdini fallback path

Houdini's `HdLegacyPrimSceneIndex` does not invoke `UsdImagingAPISchemaAdapter`,
so the `HairProceduralAPI` schema namespace is not propagated into the Hydra
scene index. As a fallback, the scene index reads the capture data directly from
primvars written by the Houdini SOP export:

| Primvar | Type | Content |
|---|---|---|
| `hairProc_prim` | `int[]` | captured face index per strand |
| `hairProc_paramuv` | `float2[]` | parametric UV per strand |
| `hairProc_rest` | `float3[]` | rest position per strand root |
| `hairProc_target` | `string[]` | target mesh scene graph paths |

When these primvars are present, `_PrimsAdded` synthesises an in-memory
`HairProcHairProceduralSchema` overlay and initialises the deformer normally.

## Build

Two mutually exclusive build modes, controlled by CMake options.

### Standalone OpenUSD

```bash
cmake -B build -S . \
  -DBUILD_USD_PLUGIN=ON \
  -DUSD_INSTALL_ROOT=/path/to/usd-install \
  -Dpeasyocl_DIR=/path/to/peasyocl

cmake --build build -j$(nproc)
cmake --install build --prefix /path/to/install
```

### Houdini plugin

```bash
cmake -B build -S . \
  -DBUILD_HOUDINI_PLUGIN=ON \
  -DHOUDINI_INSTALL_ROOT=/opt/hfsXX.X.XXX \
  -Dpeasyocl_DIR=/path/to/peasyocl

cmake --build build -j$(nproc)
cmake --install build --prefix /path/to/install
```

CMake presets are defined in `CMakePresets.json` (`ubuntu-debug`, `rh-debug`,
`windows-debug`, `macos-debug`). All use Ninja.

Required external dependencies:
- `peasyocl` — OpenCL C++ wrapper (find via `peasyocl_DIR`)
- `OpenGL` — required by hairProc
- `pxr` (OpenUSD) or Houdini — set via `USD_INSTALL_ROOT` / `HOUDINI_INSTALL_ROOT`

### OpenCL ICD note

Ensure the OpenCL ICD used at runtime matches your driver. On systems with CUDA
installed, `/usr/local/cuda/lib64/libOpenCL.so` may shadow the system ICD. Build
peasyocl explicitly against the system library to avoid conflicts:

```bash
cmake -B build -DOpenCL_LIBRARY=/usr/lib64/libOpenCL.so ...
```

## Plugin registration

`plugInfo.json` registers three types:
- `HairProcHairProceduralAPI` — USD schema
- `HairProcHairProceduralAPIAdapter` — imaging adapter
- `HairProcHairProceduralSceneIndexPlugin` — scene index plugin (all renderers, priority 1)

`plugInfoHoudini.json` is the Houdini variant; `create_target()` in
`cmake/createTarget.cmake` selects between them at configure time.

## Schema regeneration

If `schema.usda` is modified, regenerate the C++ boilerplate with:

```bash
usdGenSchema hairProc/usd/schema.usda hairProc/usd/
```

Files touched by codegen: `hairProceduralAPI.h/.cpp`, `tokens.h/.cpp`,
`generatedSchema.usda`, `module.cpp`, `moduleDeps.cpp`,
`wrapHairProceduralAPI.cpp`, `wrapTokens.cpp`.

## Test data

`hairProc/testenv/genHairProc.py` generates a test USD stage (`hairProc.usda`)
with a tube and plane as targets and 100k-strand grooms. Requires the `vik`
Python package from the install tree.

```bash
(
  SKROTVIKTOR_DIR=/home/nicholas.yue/systems/skrotViktor;
  env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProcHoudini/resources \
  LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib \
  PYTHONPATH=$SKROTVIKTOR_DIR/lib/python \
  hython hairProc/testenv/genHairProc.py
)
```

## Running Houdini with the plugin

Launch Houdini with the same environment variables so the plugin and OpenCL kernel are discoverable:

```bash
(
  SKROTVIKTOR_DIR=/home/nicholas.yue/systems/skrotViktor;
  env PXR_PLUGINPATH_NAME=$SKROTVIKTOR_DIR/lib/usd/hairProcHoudini/resources \
  LD_LIBRARY_PATH=$SKROTVIKTOR_DIR/lib \
  OCL_KERNEL_PATHS=$SKROTVIKTOR_DIR/ocl/kernels \
  houdini
)
```
