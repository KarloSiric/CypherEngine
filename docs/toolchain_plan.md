<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/toolchain_plan.md
//  Purpose: Documents toolchain plan.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Toolchain Plan

CypherEngine's toolchain should follow the runtime, not lead it.

That means:

- build runtime consumption first
- prove the data shape in engine code
- then build the offline pipeline that supports it
- keep tools focused until real workflow pressure exists

## Third-party policy

CypherEngine should write its own engine systems, but use proven libraries for
hard external formats, compression, platform glue, tests, benchmarks and tools.

Rule:

```text
third-party library -> Cypher wrapper/subsystem -> engine public API
```

Third-party APIs should not leak through public runtime headers.

Current and target choices:

- `SDL3` for window/input/platform bootstrap
- `glad` for OpenGL loading
- `Catch2` for tests
- `Google Benchmark` for performance tests
- `OpenAL Soft` for audio backend work
- `zstd` and `lz4` for compression
- `xxHash` plus Cypher checksum code for fast hashes and validation
- `libsodium` for later cryptographic signing/auth/encryption needs
- `meshoptimizer` for mesh processing
- `FreeType` and `HarfBuzz` for font/text tooling
- `libpng` and `libjpeg-turbo` for PNG/JPEG source image import
- `TinyEXR` later for HDR/EXR import if needed
- `KTX/KTX2` and Basis Universal later for serious cooked texture delivery
- `cgltf` for glTF/GLB import
- `Assimp` only for tools-side fallback importing of many model formats
- `MikkTSpace` for tangent generation
- `Opus`, `opusfile`, and `libsndfile` for voice, streams, and tools-side audio decoding
- `Dear ImGui` for debug/editor prototypes
- `Qt 6` for the long-term Mason editor
- `GameNetworkingSockets` only as an optional transport/reference later; the
  gameplay replication layer remains Cypher-owned

Reference engines often kept third-party code near the public/tooling boundary:

- Quake III had `code/jpeg-6` and common archive helpers in `qcommon`.
- Source SDK exposed folders such as `jpeglib`, `zip`, `zlib` and used public
  engine headers around those dependencies.
- Doom 3 BFG kept an explicit `external` project beside engine libraries.

CypherEngine should use `vcpkg` for large portable dependencies and `thirdparty/`
for small pinned libraries that are easier to vendor.

Approved acquisition policy:

- generated GLAD, Dear ImGui, cgltf, and MikkTSpace are pinned under `thirdparty/`
- SDL3 is the required base vcpkg dependency
- Catch2 and Google Benchmark are isolated test and benchmark features
- GLM and Lua back the future math and scripting layers
- LZ4, Zstd, xxHash, and libsodium support packages, caches, hashes, and security
- libpng, libjpeg-turbo, TinyEXR, KTX, meshoptimizer, and optional Assimp support asset cooking
- OpenAL Soft, Opus/opusfile, and libsndfile support runtime audio and source conversion
- libzip supports ordinary ZIP interchange; `.cypkg` remains a Cypher format
- curl supports HTTPS; GameNetworkingSockets remains an optional transport backend
- shaderc and SPIRV-Cross support the offline shader pipeline
- SQLite supports the editor asset database and derived-data indexes
- Qt 6, FMOD, and the Vulkan SDK are externally installed SDKs

The vcpkg registry revision is pinned by `builtin-baseline`. Optional packages are
grouped as manifest features so a Common-only test build does not compile the
future editor, asset pipeline, audio backend, and networking backend.

FMOD is not a default CypherEngine dependency. Its normal license does not permit
redistributing the SDK as part of a general game engine or tool set. Qt, OpenAL
Soft, and libsndfile also require deliberate linkage and release-license review.

## Authoring and cooked formats

Mason and command-line tools edit authoring formats. Runtime loads cooked
formats whenever parsing source data would be too slow or allocation-heavy.

Target source formats:

- `.cymap` editable Mason map source
- `.cyscene` editable scene source
- `.cyprefab` prefab/entity template
- `.cymat` material source
- `.cyshader` shader source metadata
- `.cyphys` physics setup source
- `.cynav` navigation source
- `.cyflow` objective/logic graph source

Target cooked formats:

- `.cymap_c` cooked map data
- `.cyscene_c` cooked scene data
- `.cybsp_c` compiled BSP/visibility/collision data
- `.cytex_c` cooked texture
- `.cymat_c` cooked material
- `.cymesh_c` cooked mesh
- `.cyskel_c` cooked skeleton
- `.cyanim_c` cooked animation
- `.cyphys_c` cooked collision/physics data
- `.cysnd_c` cooked sound
- `.cyfont_c` cooked font
- `.cyshader_c` shader cache metadata
- `.cynav_c` cooked navmesh
- `.cyflow_c` cooked mission/objective graph
- `.cypkg` packed game assets

All cooked formats should share a predictable binary skeleton:

```text
magic
version
endian marker
platform/backend flags
format flags
chunk table
chunks
content hash
optional compression per chunk
```

## Worlds and Maps

Target:

- authored arenas
- entity metadata
- collision-ready world representation
- visibility-ready world representation
- editor-friendly source format
- cooked runtime format when needed

Recommended order:

1. define the minimum `CypherWorld` runtime data the engine needs
2. load a simple loose world/map source file
3. instantiate static objects and entity spawn data from it
4. add collision and trace data once movement needs it
5. add visibility/spatial partition data once renderer pressure needs it
6. build `CypherMapCompiler` when hand-authored source data needs cooking
7. build editor-side tooling only when the runtime world path already exists

Important rule:

- the runtime world contract comes before the editor
- the editor edits real engine data, not a disconnected fake format

## Models

Target:

- runtime model loading
- later custom Cypher model format
- later compiler/decompiler tooling

Recommended order:

1. get visible content on screen using the simplest viable path
2. define the runtime model requirements
3. design the Cypher model format
4. build runtime model loader
5. build model compiler
6. build inspector/decompiler only if it truly helps iteration

## Textures and Materials

Target:

- predictable runtime texture loading
- material definition files
- later conversion, mip, compression, and packing tools

Recommended order:

1. simple direct texture loading
2. material file conventions
3. texture conversion/mip work
4. atlas/packing tools only when they reduce pain

## Resources

Target:

- handles for shaders, meshes, textures, materials, sounds, animations, and worlds
- centralized load/unload/reload behavior
- dependency tracking
- editor/runtime hot-reload direction
- runtime-ready cooked data where source formats are expensive

Recommended order:

1. define resource handles
2. load shader resources through the resource layer
3. load mesh/texture/material resources through the resource layer
4. track dependencies
5. add hot reload once the editor or iteration flow needs it

Important rule:

- the runtime should not permanently parse heavy source formats when an offline
  compiler can produce simpler runtime data
- the resource compiler should reduce runtime transformations and small
  allocation churn

## Scripts

Target:

- gameplay bytecode generated from the Cypher script path

Recommended order:

1. `rvm` runtime
2. assembler
3. game script bytecode
4. later language compiler
5. debug symbol support

## Archives / Packaging

Target:

- packaged shipping asset archives
- later custom Cypher package format if justified
- package index data suitable for fast runtime lookup

Recommended order:

1. direct loose-file runtime loading through `CypherFileSystem`
2. define archive format only once asset pressure exists
3. build create/list/extract tooling
4. keep archive design simple and debugger-friendly

Future tools:

- `cypherpak` for create/list/extract
- package diagnostics for missing files, opened files and mounted archives

## Editor

Target:

- Qt-based all-in-one Mason editor application
- live viewport
- world/object editing
- asset browser
- inspector
- console/CVar integration
- play-in-editor direction

Recommended order:

1. finish runtime memory/filesystem/resource foundations
2. finish basic input and camera
3. load a real simple world
4. create a Qt shell
5. embed or host a runtime viewport
6. edit real `CypherWorld` data
7. add save/load and undo/redo

## Tooling Rule

Do not build a full toolchain because it sounds impressive.

Build the specific tool when:

- the runtime path exists
- the manual workflow is painful
- the tool will clearly save future development time
