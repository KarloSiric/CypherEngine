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
- the executable target is `CypherEngine`
- the runtime can create a window, initialize OpenGL, load shaders, and submit a basic mesh path
- a CryEngine-inspired future subsystem skeleton now exists for editor, tools, resources, world, input, physics, audio, AI, animation, networking, scripting, and profiling
- Common tiers, math, security, VFS, resource contracts, render formats, and the
  tool framework are independently linkable static libraries

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
- Tier2 static schemas with exact version lookup, dynamic object maps, bounded
  validation diagnostics, and shared identifier/resource-path checks
- renderer source contracts for `.cyshader`, `.cytex`, and `.cymat`
- a versioned, explicitly serialized cooked-resource header and chunk table
- a deterministic `CYSH` cooked shader payload with validated OpenGL GLSL stage
  views and no native graphics API objects in Common
- a deterministic `CYTX` cooked texture payload with bounded PNG/JPEG/EXR import,
  semantic mip generation, independently hashed mip chunks, and strict readers
- a deterministic `CYMT` cooked material payload with canonical shader/texture
  references, typed values, hash validation, and binary-search lookups
- OpenGL context bootstrap through GLAD
- renderer lifecycle, shader, mesh, camera, and draw-list path
- vector, matrix, quaternion, bounds, ray, plane, and frustum math
- command system backend
- cvar system backend
- cfg loading/execution backend
- documentation/process system
- shared ToolFramework contracts for descriptors, invocations, compiler
  dispatch, structured host events, reports, CLI parsing/display, response files,
  terminal handling, and cooperative interrupt cancellation
- a deterministic `.cyshader` compiler backed by glslang, with exact CYKV/schema
  validation, dependency reporting, cross-stage interface checks, transactional
  `.cyshader_c` publication, and an embeddable compiler descriptor
- independently linkable `.cytex` and `.cymat` compiler modules registered with
  the same ResourceCompiler registry
- `CypherResourceCompiler` 1.0.0 with compile/validate and live compiler/format
  discovery commands, shared VFS directory and wildcard inputs, response files,
  zsh completion, branded descriptor-driven help, text/NDJSON output,
  terminal-aware ANSI color, aggregate progress and reports, cancellation,
  stable exit codes, and process integration tests
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

`M6 - Offline Resource Pipeline Foundations`

The current milestone prepares Common contracts, schemas, cooked layouts, VFS
providers, and compiler modules before any renderer backend work begins. The
provider-neutral VFS facade and loose-directory provider now have separate build
targets, so compiler libraries need only the contract while source hosts opt in
to native directory access.

## Immediate next tasks

1. preserve the verified shader, texture, and material offline contracts
2. replace the monolithic runtime source glob incrementally with explicit
   subsystem targets and narrow public include surfaces
3. preserve and extend the implemented VFS/`CypherResource` owned-load path for
   `CYSH`, `CYTX`, and `CYMT`; validated views borrow manager-owned cooked blobs
4. implement the first provider for the backend-neutral render-preview contract
   while designing the renderer service/data contract together
5. add a mesh contract only when the first visible renderer path establishes its
   exact vertex, index, bounds, and material requirements
6. preserve the approved dense Hammer-influenced Picasso V1 interface contract,
   but defer its Qt 6 implementation until the renderer exposes a real preview
   provider; Picasso will reuse both existing compiler libraries
   and separate editor-neutral texture/material cores

## Explicitly not active yet

- real custom world/map runtime implementation
- software, OpenGL, or Vulkan renderer expansion during the foundation milestone
- client/server networking
- gameplay loop
- custom model/archive tooling and Qt material/texture authoring products
- additional compiler products and custom editor workflows (the shared
  ToolFramework, shader compiler, ResourceCompiler CLI, and planned product
  source roots now exist)
- VM/game-script runtime
- full SIMD string/memory/math backend

These are all intended, but they are not the current coding target.

## Resume rule

If work pauses, resume from this file first.

Do not restart architecture design from scratch unless this file and the surrounding docs are intentionally updated with a new direction.
