<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/cyphercommon_architecture.md
//  Purpose: Defines the CypherCommon architecture direction.
//  Details: This document describes what belongs in the common/public layer,
//           what must remain in owning subsystems, and how the custom runtime
//           foundation should grow without becoming a junk drawer.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherCommon Architecture

`CypherCommon` is the public/common foundation for CypherEngine.

It has two jobs:

1. provide the custom runtime foundation used by engine, game, tools, editor and tests
2. provide public contracts and shared data types for engine subsystems

It is allowed to become large. It is not allowed to become random.

## Reference Model

The direction is inspired by how older engine codebases separated shared contracts from implementation:

- Quake III used `code/qcommon` for common runtime systems such as commands, cvars, files, messages, net channels, huffman, VM glue and collision-model loading.
- Source SDK used `src/public` as a large public contract surface, with root headers plus folders for `tier0`, `tier1`, `filesystem`, `engine`, `game`, `mathlib`, `materialsystem`, `tools`, `vgui`, `vphysics`, `vstdlib`, `zip`, `zlib` and related systems.
- Source Tier0 held low-level platform/runtime/debug/thread/memory/profiling headers.
- Source Tier1 held utility code such as `CUtlVector`, `CUtlMemory`, `CUtlBuffer`, `KeyValues`, string tools, bit buffers, checksums, convars and command buffers.
- Doom 3 BFG used `idlib` as a broad common library with containers, math, hashing, system helpers, strings, dictionaries, lexer/parser, bit messages, timers, threads and map file helpers.
- CryEngine-style common headers expose common contracts by subsystem family, such as core, math, string, system, renderer, network and serialization interfaces.

CypherEngine should learn from those structures without copying their implementation.

## Hard Boundary

`CypherCommon` may contain:

- public types
- public interfaces
- compile-time constants
- format headers
- allocator interfaces
- containers
- strings
- hashing
- parsing
- serialization helpers
- reflection metadata
- logging/assert/profiling contracts
- platform-neutral handles and descriptors

`CypherCommon` must not contain:

- OpenGL/Vulkan renderer implementation
- physics solver implementation
- Qt editor widgets
- asset cooker implementation
- image decoder implementation
- audio mixer implementation
- game networking runtime
- gameplay rules
- map editor tool logic

For renderer assets, Common owns source schemas, stable resource identities,
explicit cooked layouts, and validated borrowed views. A cooker owns source
preprocessing and dependency discovery; `CypherResource` owns runtime lifetime;
the renderer owns OpenGL/Vulkan compilation, upload, and native object handles.

The owning subsystem implements behavior. Common defines the shared contract.

Example:

```text
CypherCommon/RenderSystem/ICyRenderer.h          allowed
CypherCommon/RenderSystem/CyRenderTypes.h        allowed
CypherRenderer/OpenGL/CyOpenGLRenderer.cpp   owning subsystem
```

## Target Folder Shape

The intended shape is:

```text
src/CypherCommon/
    CypherCommon.h
    CypherCommon_Config.h
    CypherCommon_Version.h
    CypherCommon_Public.h

    Tier0/
    Tier1/
    Tier2/
    Tier3/

    Mathlib/
    Security/
    DataModel/
    Formats/
    Job/

    AssetSystem/
    ResourceSystem/
    Scene/
    World/
    Entity/
    Animation/
    AI/
    Script/

    Engine/
    FileSystem/
    RenderSystem/
        Image/
        Material/
        Texture/
    InputSystem/
    SoundSystem/
    Physics/
    Network/
    Gui/
    ToolFramework/
```

Folders should be added when they receive meaningful files. Avoid tracking large empty folder trees.

## Tier Meaning

`Tier0`

Lowest dependency layer. No containers, no filesystem, no renderer, no editor.

Owns:

- base types
- platform/compiler macros
- asserts/debug breaks
- errors/results
- memory operations
- alignment/bits/endian
- timers
- CPU/system information
- SIMD feature detection
- atomics
- threading primitives
- logging/profile/stats primitives
- stack traces and crash/minidump primitives

`Tier1`

Custom utility and standard-library replacement layer. Depends on Tier0.

Owns:

- `CyUtlVector`
- `CyUtlMemory`
- `CyUtlBuffer`
- `CyUtlString`
- `CyUtlHashMap`
- `CyUtlQueue`
- string tools
- token readers
- KeyValues-style data
- bit buffers
- command buffers
- checksums
- small utility containers

`Tier2`

Higher shared helper layer. Depends on Tier0 and Tier1.

Owns:

- file utility helpers
- render helper declarations
- mesh helper declarations
- sound helper declarations
- tool helper primitives
- config helper primitives

`Tier3`

Tool, scene, model and editor-facing helper layer.

Owns:

- model utility contracts
- scene utility contracts
- editor/game bridge helpers
- tool pipeline shared helpers

## Naming Families

Use names that identify the family and purpose.

Utility containers:

```text
CyUtlMemory
CyUtlVector
CyUtlFixedVector
CyUtlSmallVector
CyUtlBuffer
CyUtlString
CyUtlHashMap
CyUtlHashSet
CyUtlQueue
CyUtlStack
CyUtlRingBuffer
CyUtlHandleTable
```

C-style type and function pattern:

```cpp
cy_utl_vector_t
Cy_UtlVectorInit()
Cy_UtlVectorDestroy()
Cy_UtlVectorPushBack()
```

String tools:

```text
CyStrTools
CyStringView
CyFixedString
CyStringBuilder
CyName
CyPathString
CyUnicode
```

Subsystem contracts:

```text
ICySystem
ICyFileSystem
ICyRenderer
ICyAudio
ICyPhysics
ICyNetwork
ICyEditorGame
```

Format headers:

```text
CyFormatHeader
CyFormatChunk
CyPkgFormat
CyMapFormat
CyTexFormat
CyMeshFormat
CyAnimFormat
```

## Function Pointer Policy

Function pointers are a correct tool in this codebase when they describe stable C-style behavior tables.

The detailed policy is documented in
[function_pointer_policy.md](function_pointer_policy.md).

Use function pointers for:

- subsystem interfaces loaded through module/service tables
- renderer backend dispatch
- platform backend dispatch
- allocator callbacks
- command callbacks
- console variable callbacks
- file stream callbacks
- tool plugin callbacks
- VM/native bridge calls
- optional dynamically loaded module entry points

Do not use function pointers just to avoid writing a direct function call.

Good use:

```text
ICyFileSystem
    Open
    Close
    Read
    Write
```

Good use:

```text
cy_allocator_i
    Alloc
    Realloc
    Free
```

Bad use:

```text
wrapping every simple helper in a callback table
```

The rule is:

```text
Use direct functions for normal code.
Use function-pointer tables at module, backend, plugin and ownership boundaries.
```

This keeps the engine C-style and explicit without making every local call indirect.

## Public Contract Folders

The subsystem folders inside `CypherCommon` are for contracts only.

Examples:

`Asset/`

- asset IDs
- asset type IDs
- asset dependency records
- import/cook flags
- asset metadata descriptors

`Resource/`

- resource handles
- resource states
- resource lifetime flags
- shared resource descriptors

`Scene/`

- scene IDs
- scene node descriptors
- scene layer masks
- visibility flags

`World/`

- map IDs
- world bounds
- portal/area/zone public types
- compiled world chunk descriptors

`Entity/`

- entity IDs
- component IDs
- spawn parameters
- entity class descriptors

`Animation/`

- skeleton IDs
- joint IDs
- animation clip IDs
- keyframe descriptors
- pose descriptors

`AI/`

- nav area IDs
- AI agent IDs
- behavior IDs
- perception flags

`Script/`

- script values
- script function handles
- binding descriptors

`Job/`

- job IDs
- job priorities
- dependency handles
- function signatures

`Reflection/`

- type IDs
- field descriptors
- enum descriptors
- property metadata
- editor/serializer flags

## Format Direction

Editable source formats and cooked runtime formats should be distinct.

```text
.cymap       editable Mason map source
.cymap_c     cooked map data
.cyscene     editable scene source
.cyscene_c   cooked scene data
.cytex       texture source metadata
.cytex_c     cooked texture
.cymat       material source
.cymat_c     cooked material
.cymesh      mesh source metadata
.cymesh_c    cooked mesh
.cyskel      skeleton source metadata
.cyskel_c    cooked skeleton
.cyanim      animation source metadata
.cyanim_c    cooked animation
.cyphys      physics setup source
.cyphys_c    cooked collision/physics data
.cysnd       sound source metadata
.cysnd_c     cooked sound
.cyfont      font source metadata
.cyfont_c    cooked font
.cyshader    shader source metadata
.cyshader_c  shader cache metadata
.cynav       navigation source
.cynav_c     cooked navmesh
.cyflow      mission/objective/logic graph source
.cyflow_c    cooked mission/objective/logic graph
.cypak       packed game assets
```

BSP-derived data is an optional compiler intermediate or `.cymap_c` chunk. It
is not a required standalone world format. See
[map_authoring_and_mason.md](map_authoring_and_mason.md) for the authoritative
map-source, compilation, runtime-world, and Mason design.

Cooked formats share the implemented `CYRS` container version 1:

```text
fixed little-endian header
resource FourCC and version
ordered chunk table
per-chunk alignment, size, codec and flags
optional source, file and chunk content hashes
payload chunks
```

Domain payload layouts remain versioned independently from the container. See
`docs/formats/FORMAT_CATALOG.md` for maturity and
`docs/formats/RENDER_ASSETS.md` for the first implemented family.

## Third-Party Policy

Third-party code should be either:

1. managed by `vcpkg`
2. vendored under `thirdparty/` when it is small, pinned, and practical
3. isolated inside tools-only code when it is heavy

Third-party APIs must not spread through public engine runtime headers.

Allowed pattern:

```text
thirdparty/stb
src/CypherImage/CyImageDecoder.cpp
src/CypherCommon/RenderSystem/Image/CyImageTypes.h
```

Forbidden pattern:

```text
Renderer includes stb_image.h directly from public renderer headers.
Game code calls stb_image directly.
Editor code stores SDL/OpenAL/ImGui types in common public structs.
```

Current and likely third-party stack:

```text
Build/tests:       CMake, Ninja, vcpkg, Catch2, Google Benchmark
Window/input:      SDL3
OpenGL loading:    glad
Debug UI:          Dear ImGui later
Editor UI:         Qt 6 later
Image import:      stb_image, stb_image_write, TinyEXR later
Texture pipeline:  KTX/KTX2, Basis Universal later
Model import:      cgltf, Assimp tools-only
Mesh processing:   meshoptimizer, MikkTSpace
Audio:             OpenAL Soft, miniaudio/libsndfile tools-side
Compression:       zstd, lz4, miniz if needed
Hashing:           xxHash plus Cypher CRC/checksum code
Crypto:            libsodium
Networking:        Cypher UDP stack, optional GameNetworkingSockets transport later
Profiling:         Tracy plus Cypher profile counters
Database/tools:    SQLite later for editor asset registry
Fonts:             FreeType, HarfBuzz, msdfgen later
```

## Build Order

The next Common work should happen in this order:

1. finish/audit Tier0
2. finish Tier1 custom utility layer
3. add format headers
4. add Color, IO, Hash, Parse and Serialization foundations
5. add Reflection and Resource public contracts
6. add Asset, Entity, World and Scene contracts
7. add Renderer, Material, Texture, Audio, Physics and Network contracts
8. add Tools and Editor contracts

Do not jump to editor UI or gameplay implementation before the foundation can describe engine data clearly.
