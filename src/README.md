<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/README.md
//  Purpose: Defines the authoritative ownership and maturity of source modules.
//  Details: Distinguishes implemented targets from partial systems, scaffolds,
//           and future modules so directory names are not mistaken for features.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Source Modules

The `src` tree is organized by ownership. A directory does not prove that a
runtime feature exists. The CMake target, public operations, tests, and an
integrated use case are the evidence.

The maturity words in this file follow
[ADR 0006](../docs/adr/0006-runtime-subsystem-structure.md): implemented,
integrated, partial, scaffold, and planned.

## Current Runtime Foundation

| Directory | State | Build owner | Responsibility today |
| --- | --- | --- | --- |
| `CypherCommon` | implemented, broad | several `Cypher::Common*` targets | primitive types, low-level services, utilities, formats, math, VFS, resource contracts, image processing, tool framework, and security |
| `CypherSystem` | implemented and integrated | `Cypher::System` | operating-system services, paths, events, displays, windows, timing, virtual memory, and graphics-context creation |
| `CypherLog` | implemented and integrated | `Cypher::Log` | structured process logging, channels, filtering, terminal/file sinks, and flush policy |
| `CypherMemory` | implemented and integrated | `Cypher::Memory` | named arenas, pools, buckets, scratch scopes, thread wrappers, and runtime memory statistics |
| `CypherFileSystem` | implemented and integrated | `Cypher::FileSystemRuntime` | runtime mounts, native directories, package access, writes, watches, discovery, and asynchronous requests |
| `CypherPak` | implemented | `Cypher::Pak` | package format, reading, writing, compression, and validation |
| `CypherResource` | partial | `Cypher::ResourceSystem`, `Cypher::ResourceRuntime`, `Cypher::RenderResourceRuntime` | resource identity/lifetime foundation and the first cooked render-resource adapter |
| `CypherCommand` | implemented, legacy path | `Cypher::CommandLegacy` | process command registry and dispatch pending migration to the Common Tier1 command system |
| `CypherCVar` | implemented, legacy path | `Cypher::CVarLegacy` | process CVar registry pending migration to Common Tier1 typed ConVars |
| `CypherConfig` | implemented, legacy path | `Cypher::ConfigLegacy` | startup configuration execution over the legacy command/CVar path |
| `CypherRender` | partial and integrated | `Cypher::Render` | renderer frontend and current OpenGL backend; see renderer documentation for its exact feature status |
| `CypherEngine/CypherHost` | implemented and integrated | `Cypher::Host` | process composition, ordered startup/rollback/shutdown, frame ownership, built-ins, and startup diagnostics |
| `main.cpp` | integrated | `CypherEngine` | thin executable entry point only |

## Runtime Scaffolds And Planned Modules

| Directory | State | First required vertical slice |
| --- | --- | --- |
| `CypherWorld` | scaffold with accepted architecture | world lifecycle, generational object table, bounds updates, linear visibility query, and immutable render submission |
| `CypherInput` | planned | raw event ingestion, per-frame state, focus-loss handling, actions, and one movement/look consumer |
| `CypherEntity` | planned | generation-safe IDs, component storage, spawn/deferred destroy, transforms, and deterministic update |
| `CypherPhysics` | planned | static collision representation, ray/shape casts, swept player movement, and debug queries |
| `CypherAudio` | planned | device/mixer, listener, static buffer, streaming voice, spatial emitter, and failure recovery |
| `CypherFont` | scaffold with accepted boundary | cooked font, one face, shaping/fallback, measurement, bounded glyph atlas, and text draw data |
| `CypherUI` | scaffold with accepted boundary | one HUD or menu, layout, focus/navigation, input routing, text, clipping, and a bounded draw list |
| `CypherAnimation` | planned | skeleton, pose, one clip, sampling, blending, and skinning handoff |
| `CypherAI` | planned | navigation representation, bounded path query, one agent consumer, and diagnostics |
| `CypherScript` | planned | value/native-call contract and one real gameplay use case before selecting a VM |
| `CypherNetwork` | planned | transport/session framing and one measured snapshot path |
| `CypherGame` | planned | the first complete REAP gameplay loop over implemented engine services |
| `CypherClient` | planned | local presentation and prediction once networking requires a client role |
| `CypherServer` | planned | authoritative simulation and snapshot production once networking requires a server role |

`CypherPlatform`, `CypherConsole`, and `CypherProfile` are deliberately not
separate runtime targets:

- platform services belong to `CypherSystem`;
- the visible developer console belongs to `CypherUI` and consumes Command,
  CVar, and Log;
- low-level profiling primitives belong to Common, while a future capture
  service must be justified by frame aggregation and export requirements.

Particles, lights, portals, triggers, collision, terrain, and other engine
features do not automatically become top-level modules. Their initial owners
are defined in ADR 0006.

## Product And Tool Code

| Directory | State | Responsibility |
| --- | --- | --- |
| `CypherEditor` | partial | shared editor-neutral geometry core and its staged modeling architecture |
| `CypherTools` | mixed | working resource compilers, Picasso, TileEditor, shared tool libraries, and explicitly planned tool products |

Qt GUI code stays inside tool products. Runtime UI never depends on Qt.

## Source Boundary Rules

Every implemented runtime module must have:

1. an explicit source list;
2. a named library target and `Cypher::` alias;
3. declared dependencies;
4. a documented public header surface;
5. private local/backend headers that consumers cannot include accidentally;
6. tests that link the production target;
7. benchmarks where performance is part of the contract.

Recursive source globs must not decide executable ownership. Tests must not
compile duplicate copies of production source files.

New top-level modules are created with their first working vertical slice.
Earlier planning belongs in `docs/`, where its status can be stated precisely.

## Architecture References

- [Runtime subsystem structure](../docs/adr/0006-runtime-subsystem-structure.md)
- [CryEngine 1 subsystem research](../docs/cryengine1_subsystem_research.md)
- [Mathlib runtime readiness](../docs/mathlib_runtime_readiness.md)
- [Project structure](../docs/project_structure.md)
- [Subsystem catalog](../docs/subsystem_source_catalog.md)
- [World runtime module map](../docs/world_runtime_module_map.md)
