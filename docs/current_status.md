<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/current_status.md
//  Purpose: Documents current status.
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

# CypherEngine Current Status

Snapshot date: 2026-09-18

This resume point describes the integrated working tree after the first visible
renderer slices, the render-asset V2 expansion, and the first CypherTileEditor
workspace. The non-renderer verification below remains the independently
completed September 16 pass, followed by the September 18 runtime-boundary and
Mathlib readiness pass.

The detailed evidence, dependency gates, folder audit, and six-month projection
live in [six_month_engine_plan.md](six_month_engine_plan.md). This file remains the
short resume point.

## Project State

CypherEngine now has a complete standalone rendering proof from authored GLSL
through offline cooking, VFS loading, validated `CYSH`, native OpenGL program
creation, immutable pipeline state, indexed drawing, and a visible textured
cube. A second example turns authored `.cymap` data into blockout geometry and
can resolve a bounded cooked material/texture preview path. CypherTileEditor
embeds the same renderer in a Qt-owned OpenGL surface.

The engine still lacks an integrated game-facing runtime slice. The real
`CypherEngine` Host creates a System window and reports complete startup state,
but it does not yet initialize and frame `CypherRender`, own a World, submit
runtime objects, or drive player input and gameplay. The examples and editor are
working vertical slices; they do not replace Host/World integration.

## What Exists Now

- explicit CMake targets for Common tiers, Math, Security, Image, RenderFormats,
  VFS, ResourceSystem, ResourceRuntime, render-asset resources, Pak,
  ToolFramework, System, Render, Log, Memory, runtime FileSystem, legacy
  Command/CVar/Config, and Host
- the native `CypherEngine` process with Host, Log, Memory, FileSystem, Pak,
  Command, CVar, Config, System windowing/events, and a thin entry point
- deterministic `CYRS`, `CYSH` V2/V3, `CYTX` V1/V2, and `CYMT` V1/V2 cooked
  formats with compatibility readers, strict validation, tests, and benchmarks
- `CypherResourceCompiler` plus shader, texture, and material compiler modules
- VFS-backed render-resource loaders returning manager-owned cooked views
- renderer lifecycle, backend dispatch, OpenGL capability/context bootstrap,
  frame clear/present, buffers, vertex formats/layouts, VAO-backed vertex inputs,
  native programs from cooked shaders, immutable graphics pipelines, immediate
  indexed drawing, and immutable RGBA8 2D textures
- standalone cooked-shader cube and live `.cymap` preview examples, including
  finite hidden-frame smoke modes, resize handling, ordered cleanup, material
  reload, and public-API-only rendering
- a host-surface renderer boundary that borrows a framework-owned OpenGL context
  and presentation target without exposing Qt or native handles in the renderer API
- CypherTileEditor core and Qt workspace with deterministic `.cymap` V1-V3
  persistence, validation, undo/redo, generated blockout geometry, orthographic
  and embedded 3D views, sparse selections and transforms, stairs, doors, spawn
  markers, material slots/previews, settings, console, and focused tests
- Picasso texture/material authoring core and Qt GUI with focused tests; further
  product work is not on the current runtime critical path
- World/renderer ownership decisions and reserved World module boundaries, but
  no executable World operation or target
- accepted renderer-neutral Font and runtime UI boundaries, with architecture
  scaffolds rather than advertised implementations
- a documented Mathlib coordinate, transform, projection, tolerance, validity,
  determinism, and GPU-packing contract, backed by focused tests, sanitizers,
  and Release benchmarks

## What Is Done-For-Now

- Common, Math, Security, Image, cooked format, VFS, resource, and tool contracts
  needed by the active renderer slice
- the deterministic shader/texture/material offline pipeline
- synchronous resource manager ownership and VFS-backed cooked render assets
- System window/OpenGL-context services
- the first standalone OpenGL draw slice: cooked shader, program, pipeline,
  vertex input, uniform update, indexed draw, sampled RGBA8 texture, and cleanup
- Tile Editor source-map authoring, blockout geometry, and preview behavior for
  the current pre-production scope
- basic Host, Log, Memory, FileSystem, Pak, Command, CVar, and Config behavior

"Done-for-now" means sufficient to support the next slice, not a production
completion claim.

## Active Milestone

`Host + World integration after the standalone textured blockout proof`

The renderer can now synchronously create generation-checked OpenGL programs,
pipelines, textures, and indexed draws. The next runtime milestone is to put that
working path behind the real Host lifecycle and a minimal World submission
boundary. Renderer must continue to consume validated resource views and explicit
draw data; it must not parse CYKV, discover paths, or own editor documents.

## Immediate Next Tasks

The following queue advances the working renderer/editor slices into the engine
runtime without widening the renderer prematurely:

1. integrate renderer configuration, initialization, frame ownership, resize,
   minimized-window behavior, and shutdown into the real Host
2. define the smallest executable World submission record needed by one blockout
   map and keep authored Tile Editor data outside the renderer
3. connect `CypherResource` dependency ownership to loaded shader, texture, and
   material objects instead of resolving preview dependencies ad hoc
4. turn the current bounded base-color preview into a versioned `CYSH`/`CYMT`/
   `CYTX` runtime binding path with explicit failure and lifetime behavior
5. add one checked-in V2 shader/texture/material content set and exercise it
   through the command-line cooker and runtime consumer
6. define `.cymesh` and `.cymap_c` only alongside their first real cooker and
   World/runtime consumers
7. prove one controllable graybox room before adding broader renderer features

## Parallel Non-Renderer Track

Owner: collaborator/agent. Scope: verify existing subsystem behavior, add useful
benchmarks, and refine missing functionality in small dependency-ordered slices.
Renderer implementation remains owned by the project author.

Completed slice NR-01: dedicated correctness coverage for the runtime
`src/CypherMemory` pool allocator. Eight test cases and 58,552 assertions pass in
both Debug and ASan/UBSan. Release sequential/shuffled reuse benchmarks ran at
selected capacities; [results and reproduction](non_renderer_validation_2026-09-16.md)
record the limits of those local measurements. The Common Tier1 `memory_pool_t`
tests exercise a different implementation and do not cover `Mem_Pool*`.

The expanded runtime pass is complete: arena, scratch, bucket, allocator-wrapper
and global-memory lifetimes; Resource load/teardown failures; Pak validation and
publication; FileSystem async ownership/shutdown; Command/CVar/Config parsing;
Log reconfiguration; and the sectioned live startup manifest. All 232 selected
non-renderer CTest entries pass in both Debug and ASan/UBSan. Allocator wrappers,
Log, and FileSystem also pass ThreadSanitizer. Five Release benchmark executables
ran 33 workloads with five repetitions each and no reported workload errors.

The [active work log](non_renderer_work_log.md) records exact checks, fixes, and
limitations; subsequent items live in the
[non-renderer queue](six_month_engine_plan.md#non-renderer-work-queue).
Resume that queue for this track; an unfinished renderer milestone does not
block isolated non-renderer correctness work.

The September 18 structure pass removed the recursive source glob from the
`CypherEngine` executable. `main.cpp` now links `Cypher::Host`; each existing
runtime implementation group has one explicit library owner, and the affected
tests and benchmarks link those production targets instead of compiling private
copies of their `.cpp` files. `CypherSystem` remains the platform boundary,
`CypherWorld` keeps its name, and the empty Platform/Console/Profile runtime
placeholders were retired. The accompanying CryEngine 1 study records which
historical boundaries are useful and which broad legacy aggregations Cypher
should avoid.

## Explicitly Not Active Yet

- general shader variants, independent samplers, and unrestricted material binding
- a general renderer command-buffer, render graph, Vulkan backend, advanced
  lighting, shadows, or post-processing
- general mesh/world streaming, visibility, navigation, and cooked map sections
- client/server networking
- general entity/component architecture
- rigid-body physics, terrain, portals, streaming, animation, AI, or audio graphs
- gameplay/combat loop
- additional broad compiler products or unrelated Picasso/Mason expansion
- VM/game-script runtime
- full SIMD string/memory/math backend

These remain possible future work; they are not the current coding target.

## Resume Rule

If work pauses, resume from this file first. Then follow the immediate work queue
and acceptance gates in
[six_month_engine_plan.md](six_month_engine_plan.md) for the relevant track.
Do not restart architecture design from scratch or expand into a new runtime
subsystem before its stated dependencies and acceptance criteria are clear.
