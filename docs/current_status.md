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

## Project state

CypherEngine is still in early foundation stage, but the runtime stack is now more real than the old docs implied.

`CypherEngine` is the native engine runtime currently living in this repository.

Current code snapshot:

- early runtime modules live under `src/CypherEngine/`
- build succeeds through CMake
- the executable target is `cypherengine`
- the runtime can create a window, initialize OpenGL, load shaders, and submit a basic mesh path
- a CryEngine-inspired future subsystem skeleton now exists for editor, tools, resources, world, input, physics, audio, AI, animation, networking, scripting, and profiling
- `CypherCommon` has moved into a broader Tier0/Tier1 foundation with active string, char, test and benchmark work

## What exists now

- foundational types and shared error surface
- common formatted print/error helpers
- memory arenas and pools with benchmark coverage
- logging runtime
- early platform runtime helpers
- host lifecycle scaffold
- SDL3 window creation and event polling
- filesystem mount/read path
- CypherPak package archive path
- synchronous `CypherResource` runtime with type registration, generation-safe
  handles, cache identity, reference counting, transactional load rollback, and
  deterministic shutdown
- OpenGL context bootstrap through GLAD
- renderer lifecycle, shader, mesh, camera, and draw-list path
- vector, matrix, quaternion, bounds, ray, plane, and frustum math
- command system backend
- cvar system backend
- cfg loading/execution backend
- documentation/process system
- Catch2 tests and Google Benchmark targets for the current foundation

## What is done-for-now

- `CypherCommon`
  - type aliases
  - sentinel values
  - packed subsystem error representation
  - formatted print/error helpers
- `CypherMemory`
  - arena allocator API in progress
  - size helpers
  - allocation flags
  - markers and rewind model
  - allocation counters and stats direction
- `CypherLog`
  - runtime state
  - level/channel filtering
  - console/file output path
- `CypherSystem`
  - platform/compiler detection
  - path construction
  - monotonic time/sleep/local-time helper
  - SDL3 window creation and event polling
- `CypherHost`
  - startup/shutdown ownership
  - frame begin/update/render/end sequencing
- `CypherFileSystem`
  - mount table
  - loose-file read/write path
  - read-entire-file helper for shaders/assets
- `CypherRender`
  - init/shutdown state
  - in-frame state validation
  - error-coded lifecycle contract
  - GL context bootstrap
  - shader registry/loading
  - mesh upload/draw path
  - camera matrices
  - draw-list submission
- `CypherMath`
  - vector/matrix/quaternion foundations
  - bounds/ray/plane/frustum geometry helpers
- `CypherCommand`
  - command registry
  - duplicate prevention
  - fixed argument parsing
  - callback execution
- `CypherCVar`
  - fixed registry
  - typed cached values
  - flags
  - set/find/get path
- `CypherConfig`
  - file loading
  - line execution
  - `exec`
  - `set`
  - `seta`
  - fallback command dispatch

## Active milestone

`M6 - Resource Ownership And Renderer Integration`

The synchronous resource ownership foundation is implemented and tested. This
milestone is complete when one real renderer asset travels through it end to end.

## Immediate next tasks

1. implement a VFS-backed shader resource loader
2. replace renderer-facing raw shader ownership with resource handles
3. prove load, cache hit, renderer use, release, and shutdown in one integration test
4. add texture and material resource types only after the shader path is sound
5. return to input, fly-camera, and the first playable renderer loop

## Explicitly not active yet

- real custom world/map runtime implementation
- client/server networking
- gameplay loop
- custom model/material/archive tooling
- custom editor tooling
- VM/game-script runtime
- full SIMD string/memory/math backend

These are all intended, but they are not the current coding target.

## Resume rule

If work pauses, resume from this file first.

Do not restart architecture design from scratch unless this file and the surrounding docs are intentionally updated with a new direction.
