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

## `CypherCommon`

Shared engine foundation used by both client and server style code.

Expected contents:

- primitive types
- stable handles and IDs
- shared error packing
- platform-independent macros
- common string/span/helpers when they become necessary
- SIMD, endian, alignment, bit, atomic, timer and low-level performance helpers
- public interface contracts only when editor/runtime/tools need them
- cross-subsystem data that truly has no single owner

Rule:

- `CypherCommon` is not a junk drawer
- implementation belongs in the owning subsystem
- shared contracts move here only when multiple systems need them
- scalar Common code is the correctness reference before SIMD paths are added

## `CypherRender`

Owns:

- frame begin/end
- world rendering
- model rendering
- materials
- shaders
- particles
- lightmaps
- sky
- debug draw

Rule:

- the rest of the engine should not call `OpenGL` directly
- the renderer should use platform-created contexts, not leak platform APIs outward

## `CypherPlatform`

Owns:

- OS detection
- compiler/platform flags
- SDL3 bootstrap seam
- window creation
- event pump seam
- dynamic library loading
- page size and virtual memory backend
- path helpers where they are truly platform-specific

Rule:

- `CypherPlatform` is the engine/OS boundary
- `CypherSystem` should eventually orchestrate engine lifetime, not become a dumping ground for OS code

## `CypherSystem`

Owns:

- top-level engine initialization order
- subsystem startup/shutdown
- frame begin/update/render/end orchestration
- global quit/request-shutdown flow
- high-level runtime state

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
- HUD
- scoreboard
- console UI
- menus

## `CypherNetwork`

Owns:

- socket-level communication
- packet framing
- reliable/unreliable channel logic
- serialization

Both client and server depend on this layer.

## `CypherWorld`

Owns:

- custom Cypher world/map source data
- runtime level representation
- map metadata
- object placement
- static world objects
- world bounds and visibility ownership
- entity spawn data
- future cooked world format loading

Does not own:

- low-level renderer backend
- raw asset decoding
- gameplay rules

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

## `CypherConsole`

Owns:

- visible developer console UI/runtime seam
- command text entry
- history
- console output routing
- integration with `CypherCommand` and `CypherCVar`

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

## `CypherAudio`

Owns:

- audio runtime startup/shutdown
- loading sounds
- mixing
- spatial sound
- music playback

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

## `CypherProfile`

Owns:

- CPU/GPU timing scopes
- frame counters
- memory reports
- telemetry hooks
- editor-visible diagnostics

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
