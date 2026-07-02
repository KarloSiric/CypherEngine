<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/coding_style.md
//  Purpose: Documents coding style.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Coding Style

CypherEngine is written in C++20, but the code style should stay close to
disciplined C-style engine code: explicit data, explicit ownership, and no
hidden runtime cost.

## Style principles

- explicit ownership
- explicit data flow
- narrow module boundaries
- free functions over class hierarchies
- low-magic C++
- predictable runtime cost

## Naming direction

CypherEngine uses Source/id-style low-magic C/C++ naming: explicit prefixes for
pointer intent, counts, handles, booleans and storage lifetime. The goal is not
to copy any engine's code. The goal is to make call sites readable in the same
way older professional engine code is readable: the name tells you what kind of
data you are touching before you inspect the type.

Module names use `Cypher*`:

- `CypherCommon` for shared contracts, primitive types, handles, and platform-independent helpers
- `CypherSystem` for central engine startup, shutdown, frame orchestration, and subsystem ownership
- `CypherPlatform` for OS, window, filesystem backend, dynamic library, and platform-specific glue
- `CypherMemory` for arenas, pools, allocation tracking, and memory diagnostics
- `CypherFileSystem` for mounted paths, virtual paths, file handles, and package/archive access
- `CypherConsole` for developer console, commands, CVars, and runtime tweaking
- `CypherResource` for asset handles, loading, unloading, dependencies, and hot reload
- `CypherRender` for renderer front-end, draw lists, cameras, materials, meshes, and render state
- `CypherWorld` for maps, level data, world objects, and scene ownership
- `CypherEntity` for entity identity, components, and high-level object logic
- `CypherInput` for keyboard, mouse, controller, and editor/game input routing
- `CypherPhysics` for collision, traces, rigid bodies, movement, and physics scene integration
- `CypherAudio` for sound devices, playback, mixers, and audio resources
- `CypherAI` for navigation, perception, behavior, and tactical systems
- `CypherAnimation` for skeletons, clips, blending, and animation state
- `CypherNetwork` for sockets, packets, replication, prediction, and sessions
- `CypherScript` for VM/native bridging and gameplay script integration
- `CypherProfile` for profiling, counters, scopes, and performance diagnostics
- `CypherEditor` for the Qt editor application and editor-only tools

Implementation files keep the subsystem visible in the filename:

- `CypherRender_Mesh.h`
- `CypherRender_Shader.cpp`
- `CypherMemory_Arena.h`
- `CypherFileSystem_Types.h`

Function names keep explicit subsystem prefixes because the project uses free
functions rather than large class hierarchies:

- `CypherMemory_ArenaAlloc`
- `CypherRender_SubmitDrawItem`
- `CypherFileSystem_ResolvePath`
- `CypherPlatform_CreateWindow`

Backend-specific functions include the backend name:

- `CypherRenderGL_Init`
- `CypherRenderGL_MeshCreate`
- `CypherRenderVK_Init` later if Vulkan is added

## Identifier prefixes

These prefixes are now the target for new code and for subsystem migrations:

- `pName`: pointer to one object or one buffer.
- `ppName`: pointer to pointer.
- `pszName`: pointer to a zero-terminated string.
- `szName`: local/member fixed string buffer or string-like character array.
- `nName`: count, size, byte count, capacity, limit or integer quantity.
- `iName`: loop index or random-access array index.
- `bName`: boolean predicate.
- `flName`: floating point scalar.
- `hName`: opaque handle.
- `pfnName`: function pointer.
- `m_Name`: struct/class member when the type has behavior or private-like state.
- `g_Name`: process-global state.
- `s_Name`: file-local static state.

`sz` does not mean size. It means zero-terminated string. Sizes must use `n`,
for example `nPathBytes`, `nBufferSize`, `nFileCount`, `nMaxEntries`.

Output reference parameters use an `Out` suffix after the normal typed name,
for example `nBytesReadOut`, `hMountOut`, `fileInfoOut` or `traceOut`.

Generic record/object values and error/result temporaries use lowerCamel when no
data-shape prefix is useful, for example `mountInfo`, `resolveTrace`,
`buildResult` or `memoryResult`.

## Type style

- fixed engine scalars: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `usize`, `isize`, `byte`
- plain engine data: `snake_case_t`
- creation/config descriptions: `*_desc_t`
- runtime state structs: `*_state_t`
- public opaque handles: `*_handle_t`, with variables named `hMount`, `hFile`, `hRequest`
- error enums: subsystem-local names such as `fs_error_t`, `pak_error_t`, `render_error_t`
- flag bitmasks: `*_flags_t` or `CYPHER_*_FLAG_*` constants
- enum values: `OK`, `ERR_*`, or domain-specific `NAME_*` values
- constants/macros: `SCREAMING_SNAKE_CASE`
- file names: subsystem-prefixed PascalCase with an underscore for the feature file

Future interface boundaries may use CryEngine-style `I*` names only when they
represent stable editor/runtime/tool contracts:

- `IRenderer`
- `IFileSystem`
- `IConsole`
- `IResourceSystem`

Do not add an `I*` interface just because a module exists. Add it when multiple
systems need to depend on a stable contract without knowing the implementation.

## Migration rule

Naming migrations must happen one subsystem at a time. Do not rename the entire
tree with a blind text replacement. The order is:

1. Root `CypherCommon` Tier0 names.
2. Public subsystem headers.
3. Matching implementation files.
4. Tests.
5. Build and run tests before moving to the next subsystem.

## Documentation rule

Public engine-facing headers should document:
- what the type/function is for
- ownership/lifetime assumptions
- whether a helper allocates or returns a pointer into existing memory
- whether a time source is wall-clock or monotonic

## C++ features to prefer

- `std::array`
- `std::span`
- `std::string_view`
- `std::vector` where ownership is clear
- RAII in narrow, explicit cases

## C++ features to use sparingly

- exceptions
- RTTI
- heavy templates
- metaprogramming
- abstraction layers that hide runtime cost
