<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/reference_engine_lessons.md
//  Purpose: Documents reference engine lessons.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-28
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Reference Engine Lessons

This document records architecture lessons from classic engine source trees and
public SDKs used as study material.

It is a design guide, not an implementation source.

## Legal Boundary

CypherEngine may study reference engines for subsystem boundaries, toolchain
shape, naming conventions, update order, data flow, diagnostics, and editor
workflow.

CypherEngine must not copy leaked, proprietary, or license-incompatible source
code or assets. If a reference repository is described as leaked, proprietary,
non-commercial, or unclear, treat it as a read-only study artifact. Re-derive
ideas in original CypherEngine code.

## Core Lesson

A shipped engine is not just a renderer or a game loop. It is a runtime,
resource system, virtual filesystem, package layer, memory layer, diagnostics
layer, world/entity model, renderer, audio, physics, scripting, networking,
tools, and editor pipeline working together.

CypherEngine is still at the foundation stage. That is acceptable. The next
work should deepen foundations instead of chasing editor-scale features early.

## Common And Interfaces

Classic engines often separate two kinds of shared code:

- low-level common code: types, asserts, memory helpers, strings, platform
  macros, timers, atomics, endian helpers, and bit helpers
- public subsystem contracts: renderer, filesystem, resource, world, entity,
  console, script, physics, audio, network, and editor-facing interfaces

CypherEngine should keep `CypherCommon` focused on the first category for now.
When plugin, editor, tool, or DLL boundaries become real, add a deliberate
public interface layer rather than dumping all subsystem APIs into Common.

## System Orchestration

The host/system layer should eventually make startup, update, and shutdown order
explicit.

Target boot order:

1. core boot: platform, log, memory, filesystem
2. service boot: command, cvar, config, resource, stream
3. runtime boot: input, renderer, world, entity, audio, script
4. product boot: game module or editor module
5. shutdown in reverse order

Target update slots:

1. window and platform messages
2. input
3. console and command execution
4. streaming
5. resource completion
6. world
7. physics
8. AI
9. entities
10. renderer submission
11. audio
12. tools/editor hooks when running the editor

Empty function slots are useful once systems are planned, but do not add fake
subsystems before the data they operate on exists.

## Factory Boundaries

Major systems should be easy to separate later even when statically linked now.

Prefer create/shutdown boundaries like:

```cpp
CypherResource_Create(...)
CypherRender_Create(...)
CypherWorld_Create(...)
CypherEntity_Create(...)
```

Do not start with a giant global service locator. A small service table may be
useful later, but explicit ownership and passed-in dependencies should remain
the default.

## Filesystem, Packages, And Streaming

Production virtual filesystems do more than read loose files.

Long-term VFS direction:

- strict virtual path policy
- mount priority rules
- loose-file and package overlay rules
- package-backed file info
- package archive path introspection
- missing-file recording
- opened-file recording
- package directory/index statistics
- mod-root support later
- direct package read path for uncompressed entries
- tests for priority, deduplication, unmount, missing paths, and package
  fallback

Streaming belongs above VFS and below Resource:

```text
CypherPak / loose files
        ↓
CypherFileSystem
        ↓
CypherStream
        ↓
CypherResource
        ↓
Renderer / Audio / World / Script
```

The resource compiler should move expensive source-format transformation out of
runtime loads wherever practical.

## Memory

CypherMemory should stay lifetime-oriented first:

- permanent arenas
- frame arenas
- scratch arenas
- resource arenas
- world arenas
- render arenas
- pools for non-linear lifetime

The next memory work should emphasize verification and diagnostics:

- arena growth tests
- marker and rewind tests
- alignment failure tests
- overflow failure tests
- pool double-free tests
- invalid-pointer free tests
- stats and high-water reports
- subsystem/tag memory reports
- optional debug poisoning and clear-on-reset modes

Do not globally replace `malloc`, `free`, `new`, or `delete` until there is a
specific and tested need.

## Console, Commands, And CVars

Console and CVar systems are not cosmetic. They are the runtime control surface.

Long-term CVar direction:

- archive/save flags
- readonly flags
- cheat/development flags
- restart-required flags
- render-restart-required flags
- level-reload-required flags
- server-sync flags
- change callbacks
- command and cvar auto-completion
- `cmdlist`, `cvarlist`, and diagnostic dump commands

Script integration should wait until the script system exists.

## Resource Ownership

`CypherResource` is the next major seam.

Renderer resources should become engine resources with handles, lifetime,
dependency tracking, reload behavior, streaming behavior, and diagnostics.

Target direction:

```cpp
resource_handle_t
resource_type_t
resource_state_t
resource_desc_t
resource_record_t
CypherResource_Init
CypherResource_LoadShader
CypherResource_LoadMesh
CypherResource_Find
CypherResource_AddRef
CypherResource_Release
CypherResource_GetStats
```

Renderer draw submission should move away from raw `mesh_t*` and `shader_t*`
in public-facing data. Long term, draw items should reference mesh/material
handles.

## Renderer

Do not grow renderer features faster than resource ownership.

Correct order:

1. shader resource
2. texture resource
3. material resource
4. mesh resource
5. renderer consumes handles
6. debug draw
7. lighting
8. culling
9. shadows and post-processing later

Renderer objects must expose diagnostics and reload paths before the editor
depends on them.

## World And Entity

World data must precede a serious editor.

World direction:

- source world format
- runtime loaded world structs
- entity placement
- static mesh placement
- material references
- resource handles
- visibility/update policy
- serialization

Entity direction:

- identity
- lifetime
- deferred destroy
- update policy
- script binding later
- render registration
- physics binding
- audio hooks
- network presence later
- save/load semantics

Do not start with full ECS complexity before the first world path is real.

## Tools And Editor

A serious editor exists because the runtime has real data to edit.

Toolchain should come before full editor scale:

- `CypherPak`
- `CypherModelCompiler`
- `CypherTextureCompiler`
- `CypherMaterialCompiler`
- `CypherMapCompiler`
- script compiler/assembler later if the VM path survives

The editor starts after memory, filesystem, resource, renderer, input, and world
paths are stable enough to edit real engine data.

The complete Source 2 capability study and the boundary between useful workflow
lessons and blind tool parity are documented in
[source2_tooling_reference.md](source2_tooling_reference.md).

## Performance Rules

Performance work must be measured.

Rules:

- tests prove correctness
- benchmarks detect speed and regressions
- profiler scopes show frame cost
- cvars expose tuning and diagnostics
- runtime loads should avoid expensive transformation
- package indices should be preloaded for fast lookup
- asset loads should prefer fewer large allocations over many small allocations
- SIMD belongs behind a tested scalar reference path

Near-term performance focus:

1. finish Common string correctness
2. keep scalar string implementations safe and benchmarked
3. design `CypherCommon_Simd` for SSE2 and NEON
4. add profiler scopes before making broad performance claims
5. move asset loads toward Resource-owned lifetime
