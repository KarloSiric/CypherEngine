<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/development_phases.md
//  Purpose: Documents development phases.
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

# CypherEngine Development Phases

This is the detailed build order for CypherEngine and the REAP game direction.

The rule is simple:

- each phase must leave the project in a more runnable or more teachable state
- do not build deep later-phase systems before earlier runtime pressure exists
- use the codebase you actually have, not the one you imagine you already finished

## Phase 0 - Foundation Core

Build:

- basic types
- common error surface
- print/error helpers
- logging
- platform scaffold
- host scaffold
- renderer contract

Exit when:

- the project builds
- the foundation API is documented

## Phase 1 - Command and Config Backbone

Build:

- command system backend
- cvar system backend
- cfg execution/loading
- basic startup-facing config path

Exit when:

- runtime commands can be registered and executed
- cvars can be registered, queried, and changed
- cfg files can drive startup/runtime configuration

Status:

- partially complete now

## Phase 2 - Filesystem and Asset Path

Build:

- virtual filesystem
- search paths / mounts
- loose-file loading through engine-owned APIs
- cfg migration onto filesystem APIs

Exit when:

- config and future assets no longer depend on raw ad-hoc file opens everywhere

Status:

- partially complete now

## Phase 3 - Memory and Ownership Foundation

Build:

- permanent arena
- frame/scratch arena
- arena markers and rewind
- allocation counters and stats
- pool allocator design for entities/resources
- virtual memory backend direction

Exit when:

- runtime systems can allocate with explicit lifetime rules instead of ad-hoc ownership
- assets and future world data have a clear memory model

Status:

- active now

## Phase 4 - SDL3 and OpenGL Runtime Bootstrap

Build:

- `SDL3` platform bootstrap
- window creation
- event pump
- `OpenGL` context creation
- renderer bootstrap against the real backend

Exit when:

- the engine can open a visible window and run a frame loop with the intended platform/render path

Status:

- partially complete now

## Phase 5 - Local 3D Graybox

Build:

- camera
- input integration
- player movement
- one graybox arena
- debug visuals

Exit when:

- the player can move around a 3D space comfortably

## Phase 6 - Resource and Material Runtime

Build:

- `CypherResource` handles
- shader resource lifetime
- mesh resource lifetime
- texture loading
- material definitions
- resource dependency tracking
- first hot-reload direction

Exit when:

- renderer-facing assets are loaded through engine-owned resource paths instead of scattered direct calls

## Phase 7 - CypherWorld Runtime

Build:

- first custom world/map source format direction
- world metadata
- static object placement
- entity spawn data
- simple cooked/runtime world loading
- debug inspection support

Exit when:

- authored world data can replace pure hardcoded graybox assumptions

## Phase 8 - Honest Runtime Split

Build:

- clearer client/server/runtime ownership
- low-level network groundwork
- session model

Exit when:

- multiplayer architecture is no longer only theoretical

## Phase 9 - First Combat Loop

Build:

- one enemy
- one weapon
- basic damage/death
- one wave loop
- minimal HUD

Exit when:

- REAP starts to feel like a game, not just a runtime

## Phase 10 - Custom Formats and Tools

Build:

- runtime model requirements
- Cypher model format design
- Cypher material format design
- Cypher package/archive design when needed
- map/world compiler when runtime world pressure justifies it
- editor tooling when workflow pressure justifies it

Exit when:

- authored content has a defined production pipeline

## Phase 11 - CypherEditor Foundation

Build:

- Qt application shell
- editor viewport embedding
- asset browser
- property inspector
- world/object hierarchy
- editor console
- play-in-editor direction

Exit when:

- the editor can inspect and modify real engine-owned world/resource data

## Phase 12 - VM and Game Script Path

Build:

- `rvm` runtime
- assembler
- engine-side VM bridge
- gameplay scripts running through the VM path

Exit when:

- gameplay starts moving into the intended script/runtime split

## Phase 13 - Product Growth

Build:

- more enemies
- more maps
- better menus/config
- performance work
- stability work

Exit when:

- the game is stable and content-complete enough to feel real

## Solo-developer rule

At any moment:

- one active phase
- one active subsystem
- one or two active files

That is how a project this large stays sane.
