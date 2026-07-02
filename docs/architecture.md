<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/architecture.md
//  Purpose: Documents architecture.
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

# CypherEngine Architecture

CypherEngine is a layered project made of three connected bodies of work:

- `CypherEngine` engine runtime
- game logic
- tools and offline pipeline

The long-term structure follows idTech/GoldSrc-style runtime discipline, with
CryEngine-style subsystem naming adapted to modern C++.

## Top-level architecture

### `src/`

The native engine runtime.

Current and target runtime modules:

- `CypherAI`
- `CypherAnimation`
- `CypherAudio`
- `CypherCommon`
- `CypherConsole`
- `CypherCommand`
- `CypherConfig`
- `CypherCVar`
- `CypherEntity`
- `CypherFileSystem`
- `CypherHost`
- `CypherInput`
- `CypherLog`
- `CypherMath`
- `CypherMemory`
- `CypherNetwork`
- `CypherPhysics`
- `CypherPlatform`
- `CypherProfile`
- `CypherRender`
- `CypherResource`
- `CypherScript`
- `CypherSystem`
- `CypherWorld`

Runtime-adjacent application and product modules:

- `CypherEditor`
- `CypherTools`
- `CypherClient`
- `CypherServer`
- `CypherGame`

### `rvm/`

Standalone Cypher VM project.

Owns:

- VM runtime
- assembler
- disassembler
- later the REAP Script compiler

### `game/`

Gameplay scripts intended to run on the VM.

This is where high-level gameplay should live once the VM path is real:

- players
- waves
- enemies
- combat
- items
- rules

### `tools/`

Offline pipeline executables.

Owns:

- model compiler/decompiler
- texture pipeline
- archive tooling
- map/resource compilers
- shared tool code
- editor-related offline processing when justified

## Boundary rules

- `CypherCommon` is shared foundation
- `CypherMemory` owns allocator and memory lifetime policy
- `CypherPlatform` owns OS/window/time/platform-facing behavior and the SDL seam
- `CypherSystem` owns high-level engine orchestration
- `CypherRender` owns GPU-facing code
- `CypherFileSystem` owns path resolution, mounts, and file I/O
- `CypherHost` owns top-level engine orchestration
- `CypherResource` owns asset lifetime and resource handles
- `CypherWorld` owns level/world data
- `CypherEntity` owns entity/component identity and lifetime glue
- `CypherClient` owns local input/prediction/presentation bridge
- `CypherServer` owns authoritative simulation
- `CypherNetwork` owns transport and serialization primitives
- `CypherPhysics` owns movement and shared simulation code
- `CypherAudio` owns sound runtime
- `CypherAI` owns navigation/perception/behavior support
- `CypherAnimation` owns skeleton/clip/pose evaluation
- `CypherScript` is the bridge between engine runtime and `rvm`
- `CypherEditor` owns the Qt editor application and editor-only workflows

## Reference-derived architecture rules

CypherEngine studies shipped engines for structure, not source code.
The durable lessons are:

- `CypherCommon` is the primitive foundation, not a junk drawer.
- a later public-interface layer may be needed for tool/editor/plugin
  boundaries, but should not be created before those boundaries are real.
- `CypherHost` should make boot, update, render and shutdown order explicit.
- major subsystems should have clean create/shutdown boundaries even when
  statically linked.
- VFS, streaming and resources are separate layers.
- renderer-facing data should move toward handles owned by `CypherResource`.
- the editor comes after runtime data, resource handles and world format exist.

Target runtime stack:

```text
Platform / Log / Memory
        ↓
FileSystem / Pak / Command / CVar / Config
        ↓
Stream / Resource
        ↓
Render / Input / World / Entity / Audio / Script
        ↓
Game or Editor product layer
```

Target asset path:

```text
loose files / packages
        ↓
CypherFileSystem
        ↓
CypherStream
        ↓
CypherResource
        ↓
Renderer / Audio / World / Script consumers
```

## Current implementation reality

The current repository is much earlier than the target structure.

Today:

- code is concentrated in `src/CypherEngine/`
- the project has core runtime, SDL3 windowing, OpenGL bootstrap, math, shader, mesh, and camera foundations
- command/cvar/cfg/filesystem subsystems already exist as early engine services
- the next major missing seam is memory/resource ownership, input, material/texture runtime, and real world content

That is acceptable for now, as long as new work follows the documented target structure from this point forward.

## Design philosophy

- engine-owned interfaces at subsystem boundaries
- explicit data flow
- no heavy abstraction without runtime pressure
- learn from idTech, GoldSrc/Source, and CryEngine lineage without copying blindly
- long-term maintainability for a solo developer
