<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/adr/0006-runtime-subsystem-structure.md
//  Purpose: Fixes runtime module names, ownership, and creation criteria.
//  Details: Resolves the Host/System/Platform overlap, keeps the World boundary,
//           and defines when Font, UI, and other runtime modules become real.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ADR 0006: Runtime Subsystem Structure

## Status

Accepted.

## Context

At the time of this decision, the source tree mixed three different kinds of
names:

1. implemented libraries such as `CypherSystem`, `CypherRender`, and
   `CypherPak`;
2. implemented source groups that are compiled directly into the
   `CypherEngine` executable, including Host, Log, Memory, FileSystem,
   Command, CVar, and Config;
3. future directories that contain only `.gitkeep` or architecture notes.

That makes a directory listing look like a complete engine even though CMake
cannot express most of the runtime ownership. It also makes several boundaries
look duplicated. `CypherHost` and the older project documents both claim
orchestration, while `CypherSystem` and the empty `CypherPlatform` directory
both claim operating-system work.

The Far Cry-era CryEngine 1 layout is useful evidence, but it is not a template
to copy. Its narrow modules, such as CryFont, had a coherent purpose. Its large
CrySystem and CryGame modules also accumulated many unrelated responsibilities.
Cypher needs visible ownership without creating one target for every feature.

## Decision

### `CypherEngine` remains the executable

`CypherEngine` is the process/product target. Its executable source set contains
only the entry point. It does not compile the implementations of unrelated
runtime services.

The executable links `CypherHost`, which is the composition root:

```text
CypherEngine executable
        |
        v
CypherHost
        |
        +-- System / Log / Memory
        +-- FileSystem / Pak / Resource
        +-- Command / CVar / Config
        +-- Input / World / Entity / Physics
        +-- Audio / Font / UI
        `-- Render
```

`CypherHost` owns startup order, rollback, frame sequencing, shutdown order,
and process-wide policy. No reusable subsystem depends on Host.

### `CypherSystem` owns the platform boundary

`CypherSystem` remains the operating-system-facing runtime module. It owns:

- process and path services;
- timing and calendar services;
- virtual-memory forwarding;
- dynamic-library and environment access;
- system and hardware information;
- the native event pump;
- display and window management;
- graphics-context and presentation-surface creation.

Platform-specific implementation files remain private subdirectories of
`CypherSystem`. A separate `CypherPlatform` target is not created because the
current implemented System API already is that boundary. The empty
`CypherPlatform` placeholder is retired.

This decision supersedes earlier planning text that assigned engine lifecycle
orchestration to `CypherSystem`. Lifecycle orchestration belongs to Host.

### `CypherWorld` keeps its name

The runtime spatial-world module remains `CypherWorld`. It owns loaded map
state, static placement, runtime spatial proxies, terrain, environment,
visibility, portals, residency decisions, and renderer-neutral view
submissions.

It is not renamed to `Cypher3DEngine`. That historical name combines scene
ownership with an ambiguous reference to the complete engine and can be
mistaken for the renderer. `CypherWorld` states the ownership directly and is
already fixed by ADR 0004.

### Runtime text and interface modules use `CypherFont` and `CypherUI`

`CypherFont` is the accepted name for the renderer-neutral runtime text system.
It will own font-family resolution, shaping and fallback, glyph metrics, text
measurement, glyph rasterization or cooked glyph data, and glyph cache/atlas
policy. It emits text geometry or draw data; it does not issue native graphics
commands.

`CypherUI` is the accepted name for the game runtime interface system. It will
own layout, styles, focus, navigation, input routing, clipping, accessibility,
animation state, localization binding, and renderer-neutral UI draw lists.

Qt authoring interfaces in Picasso, TileEditor, and Mason remain tool code.
Dear ImGui, if used for runtime diagnostics, remains a debug overlay. Neither is
the implementation of `CypherUI`.

`CypherUI` depends on Font, Input, Resource, and Common contracts. Render
consumes its immutable draw lists. Font remains independent because consoles,
debug labels, subtitles, captions, and world-space labels also need text.

### Features stay with a coherent owner

Cypher does not create a top-level library for every noun in a game engine.
The initial ownership is:

| Feature | Owner | Reason |
| --- | --- | --- |
| lights and environment probes | `CypherWorld` records, `CypherRender` execution | World selects spatially relevant lights; Render owns GPU work |
| visibility, portals, PVS, terrain, vegetation, water | `CypherWorld` | These are spatial-world and residency concerns |
| collision shapes, casts, contacts, constraints | `CypherPhysics` | Render visibility and collision acceleration have different workloads |
| triggers and areas | `CypherEntity` identity plus World/Physics query data | Triggers are gameplay objects with spatial or physical shapes |
| simple particles and decals | World registration plus Render execution | A separate VFX target waits for an effect runtime with measurable ownership |
| cinematics | Game/Animation/Audio/UI coordination at first | A separate timeline target waits for reusable sequencing and tools |
| navigation | `CypherAI` | Navigation data and path queries support AI behavior |
| developer console presentation | `CypherUI` | Command, CVar, and Log remain independent services |
| profiling primitives | `CypherCommon` Tier0 | A separate capture service waits for frame aggregation and export |

A feature may graduate into its own target when it has a stable public
contract, independent lifetime, more than one consumer, focused tests, and a
dependency boundary that CMake can enforce.

### Every implemented subsystem gets an explicit target

The executable source glob is removed. Existing implementation groups become
named libraries before new broad systems are added. During migration, legacy
names are allowed where they make temporary duplication explicit.

The first target extraction is:

```text
Cypher::Log
Cypher::Memory
Cypher::FileSystemRuntime
Cypher::CommandLegacy
Cypher::CVarLegacy
Cypher::ConfigLegacy
Cypher::Host
```

Tests link these production libraries. They do not compile a second copy of
the production `.cpp` files.

### Module maturity is stated explicitly

Architecture documents use these terms consistently:

| State | Meaning |
| --- | --- |
| **implemented** | Public operations have production code, an owning target, and focused tests |
| **integrated** | Host or a product exercises the implemented module in a real runtime path |
| **partial** | A useful vertical slice exists, but named responsibilities remain absent |
| **scaffold** | Ownership and gates are documented; no runtime API is advertised |
| **planned** | The need is recorded, but no source directory or public contract is required |

A folder name, `.gitkeep`, interface sketch, or README alone never means that a
subsystem is implemented.

## First Vertical Slices

New systems begin with an end-to-end use case rather than a speculative API.

### World

1. One world instance with explicit lifecycle.
2. Generation-checked object handles.
3. Transform, bounds, layer mask, and render-resource references.
4. Insert, update, remove, and stale-handle behavior.
5. Linear reference frustum query.
6. Immutable renderer-neutral submission.
7. Correctness tests and a representative benchmark.

### Font

1. Versioned cooked font resource.
2. One loaded face and family record.
3. Strict UTF-8 decoding and replacement policy.
4. Glyph metrics, kerning or shaping output, and text measurement.
5. Bounded glyph cache or cooked atlas.
6. Renderer-neutral text draw data.
7. One visible line rendered by a runtime sample.
8. Tests for malformed text, missing glyphs, fallback, measurement, and cache
   exhaustion.

Text shaping must account for complex scripts before the public layout contract
is frozen. Codepoint iteration alone is not a complete text-layout system.

### UI

1. One HUD or focusable menu document.
2. Deterministic rectangle layout and clipping.
3. Pointer, keyboard, controller, and focus navigation state.
4. Styled rectangle, image, and text commands.
5. A bounded renderer-neutral draw list.
6. DPI, safe-area, localization, and accessibility inputs.
7. Input, layout, focus, clipping, and draw-list tests.

## Dependency Direction

The intended runtime direction is:

```text
Common / Math / System
          |
          v
Log / Memory / Pak / FileSystem
          |
          v
Command / CVar / Config / Resource
          |
          v
Input / Entity / Physics / Audio / Font
          |
          v
World / UI / Game
          |
          v
Host -> Render execution and presentation
```

This diagram expresses layering, not a requirement that all communication use
direct calls. Handles, immutable submissions, events, bounded queues, and
explicit service inputs remain preferred where ownership crosses subsystems.

## Consequences

- `CypherEngine` and `CypherHost` now have distinct, defensible meanings.
- `CypherSystem` no longer competes with an empty `CypherPlatform` plan.
- `CypherWorld` remains renderer-neutral and keeps its established name.
- Font and runtime UI have accepted names and dependency boundaries without
  pretending that they are already implemented.
- CMake, tests, IDEs, and documentation can distinguish implemented code from
  plans.
- New module creation requires a working vertical slice and measurable
  ownership rather than similarity to another engine's directory tree.

## References

- [ADR 0003: Runtime Naming And Target Ownership](0003-runtime-naming-and-target-ownership.md)
- [ADR 0004: World And Renderer Ownership](0004-world-renderer-ownership.md)
- [CryEngine 1 Subsystem Research](../cryengine1_subsystem_research.md)
- [World Runtime Module Map](../world_runtime_module_map.md)
- [Mathlib Runtime Readiness](../mathlib_runtime_readiness.md)
