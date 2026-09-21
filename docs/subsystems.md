<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/subsystems.md
//  Purpose: Documents subsystems.
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

# CypherEngine Subsystems

This file defines what the major CypherEngine modules are supposed to do.

Current implementation maturity is recorded in [`src/README.md`](../src/README.md).
Final naming and creation rules are fixed by
[ADR 0006](adr/0006-runtime-subsystem-structure.md).

The concrete planned implementation-unit inventory lives in
[subsystem_source_catalog.md](subsystem_source_catalog.md). That catalog records
future filenames without bulk-creating empty source files.

## `CypherCommon`

Shared public/common foundation used by runtime, tools, editor code, tests and
future game code.

Expected contents:

- Tier0 low-level runtime foundations
- Tier1 custom utility and standard-library replacement pieces
- primitive types, stable handles, IDs and shared error/result types
- platform-independent macros and compiler annotations
- strings, spans, buffers, containers and hashing utilities
- SIMD, endian, alignment, bit, atomic, timer and low-level performance helpers
- shared file format headers, magic values, chunk descriptors and version data
- public interface contracts for engine, filesystem, renderer, material, texture,
  input, audio, font, UI, physics, networking, tools and editor boundaries
- cross-subsystem descriptors for assets, resources, scenes, worlds, entities,
  animation, AI, script, jobs and reflection

Rule:

- `CypherCommon` is not a junk drawer
- implementation belongs in the owning subsystem
- shared contracts move here only when multiple systems need them
- scalar Common code is the correctness reference before SIMD paths are added
- third-party APIs are wrapped before crossing into public Cypher headers

## `CypherRender`

Owns:

- GPU resource objects and backend validation
- render passes, queues, sorting, batching, and command recording
- mesh, material, shader, texture, light, particle, UI, and debug-draw execution
- fine renderer culling and optional GPU occlusion
- presentation through a System-created surface

Rule:

- the rest of the engine should not call `OpenGL` directly
- the renderer should use platform-created contexts, not leak platform APIs outward
- World selects renderer-neutral scene candidates; Render does not traverse World
- Render executes particle and light submissions but does not own their gameplay or
  spatial lifetime

## `CypherEngine` and `CypherHost`

Owns:

- the thin process executable and entry point
- ordered subsystem startup, rollback, and shutdown
- frame begin/update/render/end sequencing
- process-wide runtime policy and built-in diagnostics
- construction of explicit subsystem dependencies

Rule:

- the executable contains only its entry point
- reusable subsystems never depend on Host
- Host owns composition; it does not absorb subsystem implementations

## `CypherSystem`

Owns:

- operating-system and platform services
- process paths, environment, dynamic libraries, and hardware information
- monotonic/calendar time and cooperative quit requests
- virtual-memory forwarding
- display discovery, window creation, and the native event pump
- graphics-context and presentation-surface creation

There is no separate `CypherPlatform` target. Platform-specific implementation
files are private parts of `CypherSystem`.

## `CypherServer`

Owns:

- authoritative simulation
- client connection management
- snapshot generation
- receiving input
- sending world state
- game VM bridge on the authoritative side

## `CypherClient`

Owns:

- local input
- prediction
- interpolation
- view/camera
- client-side effects
- scoreboard
- presentation state supplied to `CypherUI`

## `CypherNetwork`

Owns:

- socket-level communication
- packet framing
- reliable/unreliable channel logic
- serialization

Both client and server depend on this layer.

## `CypherWorld`

Owns:

- loaded and validated runtime world/map data
- runtime level representation
- map metadata
- static placement and runtime spatial proxies
- terrain, vegetation placement, water, sky, fog, and environment state
- lights, probes, portals, areas, world bounds, and coarse visibility
- renderer-neutral view submissions
- residency and world-streaming decisions
- entity spawn data
- future cooked world format loading

Does not own:

- low-level renderer backend
- raw asset decoding
- gameplay rules

The name remains `CypherWorld`; it is not renamed to `Cypher3DEngine`.

## `CypherEntity`

Owns:

- entity identity
- component storage direction
- entity/component handles
- object lifetime glue between world, gameplay, physics, audio, and rendering
- future prefab/object runtime bridge

## `CypherResource`

Owns:

- asset handles
- asset lifetime
- loading/unloading
- dependency tracking
- hot reload direction
- shader/mesh/texture/material/sound/animation/map resource registration
- resource states
- resource diagnostics and stats
- bridge from VFS/streaming to renderer/audio/world/script consumers

Does not own:

- raw GPU API calls
- source asset authoring
- editor UI

## `CypherMemory`

Owns:

- permanent arenas
- frame/scratch arenas
- pool allocators
- virtual memory backend abstraction
- allocation stats
- memory diagnostics
- subsystem/tag memory reports
- high-water tracking
- debug allocation verification where practical

## `CypherLog`

Owns:

- structured records, severity, and channels
- filtering and routing
- terminal and file sinks
- flushing and early/fallback diagnostic integration

The visible developer console is a future `CypherUI` surface over Log, Command,
and CVar. It is not a duplicate backend subsystem.

## `CypherFileSystem` and `CypherPak`

`CypherPak` owns the archive format, reading, writing, compression, integrity,
and package validation.

`CypherFileSystem` owns runtime mounts, virtual-path policy, loose/package
overlays, native directories, writable paths, discovery, watches, and
asynchronous file requests. It will expose an adapter to the provider-neutral
Common VFS contract rather than creating a second path policy.

## `CypherCommand`

Owns:

- command registration
- argument parsing
- command lookup
- callback dispatch

## `CypherCVar`

Owns:

- runtime tweakable variables
- flags
- typed cached values
- archive/readonly/cheat/development behavior
- restart-required and reload-required behavior later
- server-sync and diagnostic flags later
- change callbacks later

## `CypherConfig`

Owns:

- cfg file loading
- executing command lines from files
- startup config flow

Command, CVar, and Config currently have a working legacy runtime path. They
will converge on the instance-owned Common Tier1 command system after behavior
parity is proven.

## `CypherInput`

Owns:

- per-frame keyboard, mouse, and controller state
- press/release edges and focus-loss cleanup
- action maps, contexts, bindings, chords, axes, and dead zones
- relative mouse and text-input routing policy
- stable input prompts/glyph identities shared with UI

System produces raw platform events. Input converts them into device and action
state. Game, Client, and UI consume Input rather than native events.

## Future map/compiler work

Eventually owns:

- Cypher map/source format definitions
- cooked world format definitions
- traceline / tracebox
- collision against brush geometry
- visibility/portal/spatial partition data if needed
- map compiler toolchain

## `CypherPhysics`

Owns:

- player movement
- slide movement
- step movement
- air movement
- collision queries
- traces
- physics bodies
- trigger/contact data
- shared movement code used by both prediction and authoritative simulation

World visibility structures are not reused as the collision broadphase merely
because both contain bounds.

## `CypherAudio`

Owns:

- audio runtime startup/shutdown
- loading sounds
- mixing
- spatial sound
- music playback

## `CypherFont`

Owns:

- font faces, families, styles, weights, and fallback chains
- Unicode shaping and glyph runs
- glyph and line metrics, measurement, and wrapping support
- bounded glyph caches and atlas policy
- renderer-neutral text draw data

It does not own widgets or native GPU execution.

## `CypherUI`

Owns:

- runtime HUDs, menus, overlays, prompts, captions, and console presentation
- layout, styles, clipping, focus, navigation, and input routing
- DPI, safe-area, localization, and accessibility inputs
- renderer-neutral rectangle, image, and text draw lists

Qt authoring tools and Dear ImGui diagnostics are separate consumers and are
not the runtime UI implementation.

## `CypherAI`

Owns:

- navigation data
- perception queries
- behavior state
- combat decision helpers
- squad/wave logic support

## `CypherAnimation`

Owns:

- skeleton resources
- animation clips
- pose evaluation
- blending
- animation state machines or graphs

## `CypherScript`

Owns:

- loading the gameplay VM in the engine runtime
- trap bridge between VM and native engine code
- engine-side debugging of VM state

## Profiling and diagnostics

Common Tier0 owns low-level counters, clocks, and profiling primitives. Host and
the owning subsystems expose frame and memory statistics. A separate capture
service is created only when aggregation, trace export, or remote inspection has
a real end-to-end consumer.

## `CypherEditor`

Owns:

- Qt editor application
- editor viewports
- inspectors
- asset browser
- world/object editing tools
- editor console
- play-in-editor direction

## `CypherTools`

Owns:

- shared native tool code
- resource compiler helpers
- map compiler helpers
- asset compiler helpers

## `rvm`

Standalone virtual machine project.

Owns:

- VM runtime
- memory model
- stack
- opcodes
- loader
- debugger
- assembler
- disassembler
- later language compiler

## `game`

Owns gameplay logic intended to run on the VM:

- players
- weapons
- projectiles
- combat
- enemies
- AI
- waves
- spawn logic
- items
- triggers
- rules

## `tools`

Owns offline content pipeline:

- custom model compiler/decompiler
- texture processing
- package/archive creation and extraction
- small purpose-built editor tools
- asset build orchestration
