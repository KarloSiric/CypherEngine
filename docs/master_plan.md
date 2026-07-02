<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/master_plan.md
//  Purpose: Documents master plan.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Master Development Plan

Last updated: 2026-06-27

This document is the long-form plan for building CypherEngine into a serious game engine runtime, toolchain, and later Mason editor.

It is intentionally practical. The goal is not to copy CryEngine, idTech, Dark Engine, Source, or Source 2. The goal is to learn from their shape:

- explicit engine subsystems
- virtual filesystem and package-backed content
- data-driven resources
- serious renderer ownership
- editor/runtime separation
- tools that build real game data
- debugging, profiling, and diagnostics everywhere

The project should grow because systems need real behavior, not because a large file count looks impressive.

## Current Baseline

As of 2026-06-12, the repository has roughly:

- 21.5k lines of C/C++ source and tests
- 24k lines including documentation

Current implemented or partially implemented systems:

- `CypherCommon`
- `CypherLog`
- `CypherSystem`
- `CypherHost`
- `CypherMemory`
- `CypherFileSystem`
- `CypherPak`
- `CypherMath`
- `CypherRender`
- `CypherCommand`
- `CypherCVar`
- `CypherConfig`

Current future subsystem folders exist for:

- `CypherPlatform`
- `CypherInput`
- `CypherResource`
- `CypherWorld`
- `CypherEntity`
- `CypherPhysics`
- `CypherAudio`
- `CypherAI`
- `CypherAnimation`
- `CypherNetwork`
- `CypherScript`
- `CypherProfile`
- `CypherConsole`
- `CypherEditor`
- `CypherTools`

## What "Grade-A" Means Here

For this project, Grade-A does not mean matching a AAA commercial engine feature-for-feature.

It means:

- the runtime builds consistently
- major subsystems have clear ownership
- APIs return explicit errors instead of failing silently
- memory lifetimes are intentional
- assets load through engine-owned paths
- packages and loose files use one virtual namespace
- renderer, world, physics, audio, and resources interact through stable contracts
- tests cover core behavior
- debugging and diagnostics are built into the engine
- the engine can ship a real game, not only demos
- tools and editor can be added without rewriting the runtime

## Reference Engine Lessons

These are architectural lessons only. Do not copy implementation code.

### idTech 3 / Quake III

Useful lessons:

- small, explicit C-style systems
- virtual filesystem and pak search paths
- command/cvar/config backbone
- clean client/server thinking
- data-oriented renderer submission
- game/runtime boundary
- console-driven development workflow

What to avoid:

- copying old platform assumptions blindly
- locking into old fixed-function rendering ideas
- assuming old networking decisions are enough for modern needs

### Early CryEngine

Useful lessons:

- large subsystem separation
- editor and runtime grown together over time
- resource compiler and cooked assets
- terrain, vegetation, renderer, AI, physics, and editor treated as first-class systems
- many files because tooling, diagnostics, formats, and platform paths create real complexity

What to avoid:

- chasing file count
- adding giant interfaces before runtime pressure exists
- building editor UI before runtime data is stable

### Dark Engine / Looking Glass Style Systems

Useful lessons:

- world simulation matters
- entity/object data can drive emergent gameplay
- tools and authored data are central
- gameplay systems need editor-visible data eventually

What to avoid:

- opaque data flows
- hard-to-debug object communication

### Source / Source 2 Style Direction

Useful lessons:

- editor and tools need strong asset database behavior
- packages, resources, maps, materials, and compiled formats are a production pipeline
- hot reload and inspection are not optional long term
- viewport editing is only the visible part of a much larger toolchain

What to avoid:

- starting with the editor before the runtime can load, render, simulate, and save real data

## Total Size Targets

LOC is not the goal, but LOC helps understand scope.

| Scope | Approx LOC | Meaning |
|---|---:|---|
| Current project | 21k-25k | early foundation |
| First serious runtime | 80k-150k | can build a playable FPS loop with engine-owned loading/render/world basics |
| Strong solo runtime | 180k-350k | resource system, renderer, physics integration, audio, profiling, gameplay framework |
| Runtime plus tools | 250k-500k | asset compiler, map compiler, pak tool, resource compiler, import/cook path |
| Runtime plus editor | 400k-800k | serious Mason editor with viewport, inspector, asset browser, editing tools |
| Long-term engine ecosystem | 600k-1M+ | advanced editor, networking, scripting, full pipeline, content, platform polish |

The first real target is not 900k LOC.

The first real target is:

- 100k useful LOC
- one playable vertical slice
- one reliable asset pipeline
- one renderer path that can draw actual game content

## Work Method

Use this process for every subsystem:

1. Define the subsystem boundary.
2. Write the public API first.
3. Write the data structures.
4. Write pseudocode for the important flows.
5. Implement the smallest real version.
6. Add smoke tests.
7. Add edge case tests.
8. Add diagnostics.
9. Document the policy.
10. Commit in small chunks.

Rules:

- one active subsystem at a time
- one active milestone at a time
- do not jump into editor work before runtime data is real
- do not build abstractions without pressure
- make all failure modes explicit
- keep code C++20, but C-style and data-oriented
- use free functions and structs by default
- avoid inheritance unless a stable interface truly needs it
- prefer engine-owned handles over raw object ownership in public APIs

## Immediate Schedule

Dates are written from the current project date: Friday, 2026-06-12.

### Saturday, 2026-06-13 - VFS And Package Closure

Main goal:

- finish `CypherFileSystem` and `CypherPak` to a clean runtime-core level

Implement or audit:

- every public VFS API returns a real result or deliberate unsupported error
- package unmount path
- package mount diagnostics
- duplicate mount behavior
- path normalization policy
- mount priority policy
- write-path policy
- directory listing across loose mounts and packages
- recursive find behavior
- package-backed `GetFileInfo`
- package read-only enforcement
- package fallback to loose file after unmount
- optional package mount behavior
- tests for invalid paths
- tests for package/list/find/copy edge cases

Document:

- `docs/filesystem_policy.md`
- `docs/cypak_format.md`

Do not start:

- renderer expansion
- editor
- networking
- gameplay

Exit criteria:

- CMake build passes
- CTest passes
- VFS policy is documented
- CypherPak format is documented
- no vague package/VFS stubs remain without explicit unsupported errors

### Sunday, 2026-06-14 - VFS/Pak Cleanup And Memory Re-Entry

Main goal:

- finish leftovers from Saturday, then return to memory planning

Work:

- update README/current status if needed
- add changelog entries
- clean CMake/test organization if needed
- review `CypherMemory` state
- write exact memory completion checklist

Exit criteria:

- VFS/Pak is closed for this phase
- memory next-step checklist is clear

### Monday, 2026-06-15 To Sunday, 2026-06-21 - Memory Completion

Main goal:

- finish the runtime memory foundation

Implement:

- arena allocator final audit
- arena markers
- arena rewind/reset
- pool allocator tests
- bucket allocator design and first implementation
- scratch allocator
- thread-local scratch allocator
- allocation stats
- high-water marks
- leak report path
- debug guards/canaries where practical
- subsystem allocation tags

Exit criteria:

- memory smoke tests pass
- every allocator has documented lifetime rules
- memory reports can show allocation counts and peak use

### Monday, 2026-06-22 To Sunday, 2026-06-28 - Common, Platform, Diagnostics

Main goal:

- strengthen the shared foundation before resources and renderer grow

Implement:

- assert policy
- endian helpers
- bit helpers
- alignment helpers
- handle helpers
- small string/path helpers where justified
- platform file-time helpers
- thread/mutex abstraction
- profiler scope API skeleton
- diagnostic dump route

Exit criteria:

- common helpers are useful but not a junk drawer
- thread primitives exist for VFS/memory/resource use

### Monday, 2026-06-29 To Sunday, 2026-07-05 - ResourceManager Design

Main goal:

- introduce `CypherResource` as the bridge between VFS and renderer/game systems

Design:

- resource handle format
- resource type registry
- resource state enum
- load/unload flow
- dependency model
- reload model
- memory ownership
- error reporting

Implement:

- basic resource manager API
- binary/text load through VFS
- shader resource path
- smoke tests for load/unload/reload failure behavior

Exit criteria:

- renderer assets start moving away from direct file ownership

## 2026 Roadmap

### July 2026 - Resources And Renderer Ownership

Focus:

- `CypherResource`
- renderer asset ownership
- shader/mesh/texture/material loading

Implement:

- resource handles
- resource tables
- resource lifetime states
- shader resource
- texture resource
- mesh resource
- material definition
- renderer consumes resource handles instead of raw ad-hoc paths
- first hot reload direction

Approx added LOC:

- 8k-20k

Exit criteria:

- shaders, textures, meshes, and materials load through VFS/resource path

### August 2026 - Renderer Runtime Path

Focus:

- make rendering useful for a real game prototype

Implement:

- render backend boundary
- OpenGL renderer cleanup
- shader program cache
- texture upload
- mesh upload
- material binding
- render queue
- camera integration
- debug draw
- simple lighting
- sky/background path
- basic frustum culling

Approx added LOC:

- 12k-35k

Exit criteria:

- engine can render textured/material objects from resource-owned data

### September 2026 - Input, Camera, World, Entity Foundation

Focus:

- make a controllable 3D runtime

Implement:

- input mapping
- keyboard/mouse state
- action bindings
- fly camera
- player camera
- entity IDs
- transform component
- world object list
- simple scene save/load draft
- debug object spawning

Approx added LOC:

- 10k-25k

Exit criteria:

- player can move through a simple 3D scene made of engine-owned objects

### October 2026 - Physics And Collision Foundation

Focus:

- movement and collision first, full rigid body simulation later

Implement:

- collision query API
- raycast
- swept box/capsule draft
- player movement
- slide movement
- step movement
- trigger volumes
- debug collision draw
- shared movement code for future prediction/server use

Decision:

- decide whether `CypherPhysics` remains custom for movement/collision and later wraps a third-party rigid body engine such as Jolt/Bullet-style integration

Approx added LOC:

- 10k-30k

Exit criteria:

- the player collides with the world reliably enough for FPS gameplay tests

### November 2026 - World Format And First Gameplay Loop

Focus:

- stop hardcoding the whole world

Implement:

- source world format draft
- runtime world format draft
- simple map loader
- spawn points
- static mesh placements
- one weapon
- one enemy placeholder
- damage/death
- wave loop
- minimal HUD/debug UI

Approx added LOC:

- 15k-40k

Exit criteria:

- REAP has a playable graybox loop

### December 2026 - Stability, Profiling, Packaging, CI

Focus:

- make the runtime reliable enough to grow

Implement:

- CPU profiler scopes
- frame timing
- memory reports
- resource reports
- package build/test polish
- CI matrix expansion if dependencies are ready
- sanitizer builds where practical
- crash/error reporting policy
- documentation pass

Approx added LOC:

- 8k-20k

Exit criteria:

- foundation year ends with a stable runtime and playable graybox loop

## 2027 Roadmap

### Q1 2027 - Asset Pipeline And Renderer Growth

Focus:

- assets become a pipeline, not loose experiments

Implement:

- `CypherAssetCompiler`
- `CypherResourceCompiler`
- package builder tool
- material compiler
- texture processing path
- model import path
- cooked mesh format
- cooked material format
- renderer resource dependency tracking
- better lighting path
- shadow draft
- particles draft

Approx added LOC:

- 30k-80k

Exit criteria:

- authored assets can be imported, cooked, packaged, loaded, and rendered

### Q2 2027 - World, Physics, Audio, Animation

Focus:

- move from demo runtime to game runtime

Implement:

- stronger world representation
- spatial partitioning
- scene queries
- physics body integration or stronger custom collision
- audio device startup
- sound resources
- 2D/3D sound playback
- streaming music path
- skeleton resource draft
- animation clip loading draft
- animation pose evaluation draft

Approx added LOC:

- 40k-100k

Exit criteria:

- world, physics, audio, and animation can support a small FPS prototype

### Q3 2027 - Networking, Script Boundary, Tooling Prep

Focus:

- future multiplayer and gameplay code boundaries

Implement:

- socket layer
- packet format
- reliable/unreliable channel design
- snapshot serialization
- client/server ownership rules
- prediction planning
- VM/game script API design
- command/cvar remote/debug rules
- editor data contracts

Approx added LOC:

- 30k-90k

Exit criteria:

- client/server model is no longer theoretical
- gameplay scripting direction is clear
- editor contracts are known

### Q4 2027 - Mason Foundation

Focus:

- start the editor only after runtime data is usable

Implement:

- Qt application shell
- engine runtime embedded viewport
- asset browser
- inspector panel
- console/log panel
- world hierarchy panel
- selection model
- transform gizmo draft
- open/save world
- play-in-editor direction

Approx added LOC:

- 40k-120k

Exit criteria:

- Mason can inspect and modify real engine world/resource data

## 2028 Roadmap

### Q1 2028 - Editor Usability

Focus:

- make Mason useful, not just visible

Implement:

- undo/redo
- prefab/object editing
- material editing
- asset import UI
- package build UI
- viewport picking
- transform tools
- property reflection/data description layer
- editor camera controls

Approx added LOC:

- 50k-150k

### Q2 2028 - Toolchain Production Path

Focus:

- make content production repeatable

Implement:

- map compiler
- resource compiler dependency graph
- asset database
- incremental cooking
- package manifests
- package compression
- package verification
- build profiles for dev/release/mods

Approx added LOC:

- 40k-120k

### Q3 2028 - Game Feature Expansion

Focus:

- turn the prototype into a real game

Implement:

- more weapons
- more enemy types
- AI behavior
- encounter/wave authoring
- pickups/items
- UI/menu flow
- save/config profiles
- performance optimization
- content validation

Approx added LOC:

- 40k-120k

### Q4 2028 - Product Hardening

Focus:

- make the engine and game robust

Implement:

- crash handling
- better profiling
- GPU timings
- asset validation
- automated content tests
- packaging for release
- mod loading policy
- documentation
- example project

Approx added LOC:

- 30k-100k

## 2029 And Beyond

Long-term expansion:

- more advanced renderer
- terrain
- vegetation
- navigation mesh
- animation graphs
- cinematic tools
- multiplayer polish
- full script toolchain
- editor plugins
- source control integration
- live collaboration features only if truly needed

This is where the project can naturally move toward 500k+ LOC.

## Subsystem Plan

### CypherCommon

Purpose:

- shared primitive types and contracts

Implement:

- primitive type aliases
- error packing
- assert macros
- alignment helpers
- endian helpers
- bit helpers
- handle helpers
- result helpers
- safe string/span helpers only when needed

Avoid:

- making `CypherCommon` a dumping ground
- putting subsystem logic here

Approx final runtime LOC:

- 5k-20k

### CypherMemory

Purpose:

- explicit lifetime and allocation policy

Implement:

- heap wrapper
- arena allocator
- arena markers
- arena rewind/reset
- pool allocator
- bucket allocator
- scratch allocator
- thread-local scratch allocator
- frame allocator
- allocation tags
- stats and high-water marks
- leak reports
- guard bytes/canaries in debug
- allocator tests

How to implement:

- start with simple contiguous allocators
- add diagnostics after correctness
- add thread safety by policy, not by blindly locking everything
- use subsystem tags for reports

Approx final runtime LOC:

- 10k-40k

### CypherFileSystem

Purpose:

- one virtual namespace for engine content

Implement:

- path normalization
- path join/split helpers
- loose directory mounts
- package mounts
- mount priorities
- read-only/writeable policy
- write path
- open/read/write/seek/tell/close
- file info
- directory listing
- recursive find
- copy/rename/delete
- create/remove directories
- diagnostics
- file watching
- thread safety
- async reads later

How to implement:

- normalize internally at every API boundary
- never expose physical paths unless explicitly requested for tools/debug
- package files are read-only
- write operations go to the write path
- mount priority decides overrides

Approx final runtime LOC:

- 10k-35k

### CypherPak

Purpose:

- engine-owned package archive format

Implement:

- `CYPACKAGE10` header
- index table
- string table
- file entries
- hashes
- reader
- writer
- package info
- corruption validation
- compression
- streaming reads
- package manifest
- package build tool
- package dump/extract tool
- mod/package priority policy

How to implement:

- keep the archive format stable and documented
- support uncompressed first
- add compression only when dependency choice is deliberate
- use VFS for runtime access
- use tools for package creation later

Approx final runtime/tool LOC:

- 10k-50k

### CypherResource

Purpose:

- asset lifetime and dependency ownership

Implement:

- resource handles
- resource type registry
- load states
- unload/reload
- dependency tracking
- generation counters
- reference/use counters where needed
- shader resources
- texture resources
- mesh resources
- material resources
- sound resources
- world/map resources
- hot reload hooks

How to implement:

- VFS loads bytes
- resource system owns lifetime
- renderer consumes GPU-ready resources
- tools produce cooked resource data

Approx final runtime LOC:

- 20k-70k

### CypherPlatform / CypherSystem / CypherHost

Purpose:

- OS boundary and engine lifecycle

Implement:

- window ownership
- event pump
- clock/time
- sleep
- threads
- mutexes
- atomics policy
- dynamic library loading
- process helpers for tools
- engine init/shutdown order
- frame loop
- subsystem dependency order

Approx final runtime LOC:

- 10k-35k

### CypherJobs

Purpose:

- background work and parallelism

Implement:

- thread pool
- work queues
- job handles
- dependencies
- worker-local scratch
- async file jobs
- resource load jobs
- profiling integration

Approx final runtime LOC:

- 8k-30k

Note:

- A `CypherJobs` folder may be worth adding when resource loading and renderer preparation need it.

### CypherRender

Purpose:

- draw the world

Implement:

- backend boundary
- OpenGL backend
- shader compilation/linking
- shader reflection or binding declarations
- texture upload
- mesh upload
- material binding
- render queues
- camera
- culling
- debug draw
- lighting
- shadows
- sky
- particles
- postprocessing
- GPU profiler
- renderer resource lifetime
- eventual backend upgrade path

How to implement:

- keep the first backend simple
- make renderer consume resources, not raw files
- isolate OpenGL calls inside renderer backend files
- keep draw submission explicit and data-oriented

Approx final runtime LOC:

- 50k-180k

### CypherMath

Purpose:

- engine geometry and numerical helpers

Implement:

- vectors
- matrices
- quaternions
- planes
- rays
- bounds
- frustums
- transforms
- interpolation
- collision math
- SIMD later only when profiling proves it

Approx final runtime LOC:

- 5k-20k

### CypherInput

Purpose:

- input state and action mapping

Implement:

- keyboard state
- mouse state
- controller state later
- action binding
- input contexts
- console/editor/game input routing
- mouse capture

Approx final runtime LOC:

- 5k-20k

### CypherWorld

Purpose:

- loaded map/world state

Implement:

- world metadata
- static object placement
- entity spawn data
- spatial partitioning
- world bounds
- scene queries
- save/load
- runtime world format
- source world format
- debug inspection

Approx final runtime LOC:

- 20k-80k

### CypherEntity

Purpose:

- runtime object identity and component storage

Implement:

- entity IDs
- generation handles
- transform storage
- component registration
- component arrays
- spawn/despawn
- hierarchy or parent links if needed
- prefab/object bridge later

Approx final runtime LOC:

- 15k-60k

### CypherPhysics

Purpose:

- movement, collision, and physical queries

Implement:

- raycasts
- shape casts
- player movement
- step/slide movement
- trigger volumes
- collision layers
- rigid body integration later
- debug draw
- network/prediction-friendly movement functions

How to implement:

- first solve FPS movement and collision
- do not write a full commercial rigid body engine too early
- keep a clean facade so a third-party physics backend can be integrated if needed

Approx final runtime LOC:

- 15k-80k if mostly integration
- 100k+ if writing full rigid body physics from scratch

### CypherAudio

Purpose:

- sound playback and audio resources

Implement:

- audio device startup
- sound resource loading
- short sound playback
- streamed music
- spatial audio
- listener state
- channels/mix groups
- debug controls

Approx final runtime LOC:

- 10k-40k

### CypherAI

Purpose:

- enemy behavior and navigation support

Implement:

- perception queries
- behavior states
- simple decision logic
- wave/encounter helpers
- navigation representation later
- debug visualization

Approx final runtime LOC:

- 10k-60k

### CypherAnimation

Purpose:

- skeletal animation and gameplay poses

Implement:

- skeleton resource
- clip resource
- pose buffers
- sampling
- blending
- animation events
- state machine or simple graph
- renderer skinning integration

Approx final runtime LOC:

- 20k-80k

### CypherNetwork

Purpose:

- multiplayer-capable transport and replication foundation

Implement:

- sockets
- packet framing
- reliable/unreliable channels
- serialization
- snapshots
- delta compression
- client prediction
- interpolation
- server authority
- connection state
- debug network stats

Approx final runtime LOC:

- 25k-100k

### CypherScript / RVM

Purpose:

- gameplay script or VM boundary

Implement:

- VM runtime
- bytecode format
- loader
- native trap bridge
- script debug info
- assembler/disassembler
- gameplay API

Approx final runtime/tool LOC:

- 30k-120k

### CypherProfile

Purpose:

- performance visibility

Implement:

- CPU timing scopes
- GPU timing scopes
- frame stats
- memory reports
- resource reports
- file I/O stats
- network stats
- on-screen stats
- editor-visible profiler later

Approx final runtime LOC:

- 8k-30k

### CypherTools

Purpose:

- shared offline content pipeline code

Implement:

- asset compiler helpers
- resource compiler helpers
- package compiler helpers
- map compiler helpers
- shared command-line parsing
- shared diagnostics
- build profiles

Approx final tool LOC:

- 30k-150k

### Mason

Purpose:

- full editor application

Implement:

- Qt shell
- embedded engine viewport
- asset browser
- world hierarchy
- inspector
- transform gizmos
- material editor
- package/build panels
- console/log panels
- profiler panels
- file watching/hot reload UI
- undo/redo
- prefab/object workflow
- play-in-editor
- world save/load
- validation tools

How to implement:

- do not start as a huge UI shell
- start only after resources/world data exist
- every editor feature must modify real engine-owned data
- runtime APIs must remain usable without editor

Approx final editor LOC:

- 100k-400k+

### REAP Game Runtime

Purpose:

- prove the engine by building a real game

Implement:

- player movement
- weapons
- enemies
- waves
- damage
- pickups
- arena rules
- UI/HUD
- menus
- settings
- save/config
- game data

Approx game LOC:

- 30k-150k

## Current Phase Completion Targets

### VFS/Pak Complete For Current Phase

Complete when:

- path policy is documented
- package format is documented
- all public APIs have expected behavior
- unsupported operations return explicit unsupported errors
- mount priorities work
- package unmount works
- directory listing works across loose and package data
- recursive find works
- package reads are read-only
- package info and file info work
- tests pass

Not required yet:

- compression
- streaming package reads
- package editor UI
- full asset database

### Memory Complete For Current Phase

Complete when:

- arena, pool, bucket, scratch paths work
- markers and rewinds work
- thread-local scratch exists
- debug statistics exist
- leaks can be reported
- tests cover normal and invalid usage

Not required yet:

- full platform virtual memory backend
- advanced lock-free allocators
- editor memory visualizer

### Resource Complete For First Runtime Phase

Complete when:

- resources are loaded through VFS
- handles are stable
- shaders/textures/meshes/materials have resource paths
- renderer consumes resources cleanly
- failed loads report useful errors
- reload direction exists

Not required yet:

- full asset database
- all import formats
- editor integration

## Testing And CI Plan

Short term:

- keep smoke tests for each subsystem
- run CMake build locally before commits
- run CTest before pushes
- GitHub Actions macOS build/test

Near term:

- add Linux CI once SDL3 dependency path is clean
- add Windows CI once the platform path is ready
- add sanitizer builds
- add warning-as-error job when noise is controlled

Long term:

- resource load tests
- package corruption tests
- renderer headless tests where possible
- asset compiler tests
- map compiler tests
- gameplay simulation tests

## Documentation Plan

Maintain these docs:

- `docs/current_status.md`
- `docs/roadmap.md`
- `docs/development_phases.md`
- `docs/master_plan.md`
- subsystem policy docs
- API reference docs
- changelog

Policy:

- every serious subsystem gets a policy doc
- every file format gets a format doc
- every major system gets tests before being called stable

## Commit And Git Policy

Current project rule:

- commit directly to `main` when requested
- one changed file per commit when requested
- do not commit `AGENTS.md`
- do not create feature branches unless explicitly asked
- run build/tests before pushing

## What Not To Do Yet

Do not start these before their prerequisites:

- full editor before resources/world are real
- advanced networking before a local game loop exists
- full rigid body physics before movement/collision pressure exists
- custom scripting VM before native gameplay loop proves API needs
- terrain/vegetation before renderer/world basics are solid
- massive abstraction layers before at least two systems need them

## The Line To Draw

The correct path is:

1. finish foundation
2. make assets load through the engine
3. make the renderer draw engine-owned resources
4. make a controllable world
5. make collision and gameplay work
6. make tools for the content pipeline
7. start Mason
8. grow editor and runtime together

The engine becomes large naturally when it has real coverage:

- runtime
- tools
- editor
- game
- tests
- diagnostics
- documentation

The project should chase finished behavior, not file count.
