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

CypherEngine uses id-style module-prefixed C/C++ naming. The goal is not to copy
historical source mechanically. The useful property is that ownership is visible
at a call site: `Sys_*` performs an operating-system operation, `R_*` belongs to
the renderer frontend, and `GL_*` is private OpenGL implementation code.

The compiler already knows whether a value is a pointer, integer or boolean.
Identifiers therefore describe meaning and lifetime instead of repeating the
type with faux-Hungarian prefixes. Prefer `window`, `backend`, `byteCount`,
`enabled`, `handle` and `debugName` over `pWindow`, `pBackend`, `nBytes`,
`bEnabled`, `hHandle` and `pszDebugName`.

Module names use `Cypher*`:

- `CypherCommon` for shared contracts, primitive types, handles, and platform-independent helpers
- `CypherHost` for central engine startup, shutdown, frame orchestration, and subsystem ownership
- `CypherSystem` for OS, window, timing, process, dynamic-library, and platform-specific services
- `CypherMemory` for arenas, pools, allocation tracking, and memory diagnostics
- `CypherFileSystem` for mounted paths, virtual paths, file handles, and package/archive access
- `CypherCommand`, `CypherCVar`, and `CypherConfig` for the runtime control surface
- `CypherResource` for asset handles, loading, unloading, dependencies, and hot reload
- `CypherRender` for renderer front-end, draw lists, cameras, materials, meshes, and render state
- `CypherWorld` for maps, level data, world objects, and scene ownership
- `CypherEntity` for entity identity, components, and high-level object logic
- `CypherInput` for keyboard, mouse, controller, and editor/game input routing
- `CypherPhysics` for collision, traces, rigid bodies, movement, and physics scene integration
- `CypherAudio` for sound devices, playback, mixers, and audio resources
- `CypherFont` for shaping, font metrics, fallback, glyph caches, and text draw data
- `CypherUI` for runtime HUD/menu layout, focus, navigation, accessibility, and UI draw data
- `CypherAI` for navigation, perception, behavior, and tactical systems
- `CypherAnimation` for skeletons, clips, blending, and animation state
- `CypherNetwork` for sockets, packets, replication, prediction, and sessions
- `CypherScript` for VM/native bridging and gameplay script integration
- `CypherEditor` for the Qt editor application and editor-only tools

The visible developer console is a `CypherUI` surface over Command, CVar, and
Log. Profiling primitives live in Common until capture and trace export justify
a separate runtime service. See ADR 0006 for module-creation rules.

Implementation files keep the subsystem visible in the filename:

- `CypherRender_Mesh.h`
- `CypherRender_Shader.cpp`
- `CypherMemory_Arena.h`
- `CypherFileSystem_Types.h`

Runtime functions use the short owner prefixes defined by
`adr/0003-runtime-naming-and-target-ownership.md`:

- `Mem_ArenaAlloc`
- `R_SubmitDrawItem`
- `FS_ResolvePath`
- `Sys_CreateWindow`

Renderer prefixes carry distinct architectural meanings:

- `R_*` is renderer frontend, resource, scene-building, or backend-neutral work.
- `RB_*` executes prepared renderer commands in a backend; it is not a generic
  prefix for every backend-related helper.
- `GL_*` is private OpenGL renderer code and OpenGL state management.
- `GLimp_*` is the System-owned platform integration used to create a context,
  resolve OpenGL procedures, set swap policy, and present a window.
- `RE_*` is reserved for a future engine-to-renderer module export boundary. It
  is not used merely because a function is declared in a public header.

## Identifier shape

- Variables, parameters and fields use descriptive `lowerCamelCase`.
- Types use `snake_case_t` while C-style module functions use `Prefix_PascalCase`.
- Pointer names do not receive a `p` or `pp` prefix. Indirection is visible in
  the declaration; names such as `source`, `backendOut`, and `userData` describe
  the role of the pointee.
- Function-pointer table members use operation names such as `Init`,
  `BeginFrame`, and `Shutdown`, not `pfnInit` or `pfnShutdown`.
- Boolean names state a predicate such as `initialized`, `frameActive`, or
  `allowFallback`; they do not receive a `b` prefix.
- Counts and sizes include units or meaning, such as `byteCount`, `fileCount`,
  `capacity`, `width`, and `height`; they do not receive a generic `n` prefix.
- Indexes use a meaningful name such as `extensionIndex` or `surfaceIndex`.
- Output parameters use an `Out` suffix when the direction is not obvious from
  the operation, for example `backendOut`, `bytesReadOut`, or `traceOut`.
- File-local state should have one strong subsystem name such as `tr`,
  `backEnd`, or `glState`; generic `g_` and `s_` prefixes are not required.
- Constants and enum bits use module-prefixed `SCREAMING_SNAKE_CASE`.

## Type style

- fixed engine scalars: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `usize`, `isize`, `byte`
- plain engine data: `snake_case_t`
- creation/config descriptions: `*_desc_t`
- runtime state structs: `*_state_t`
- public opaque handles: `*_handle_t`, with variables named `mountHandle`, `fileHandle`, `requestHandle`
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
