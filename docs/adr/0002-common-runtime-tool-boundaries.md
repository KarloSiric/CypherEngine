<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/adr/0002-common-runtime-tool-boundaries.md
//  Purpose: Defines ownership boundaries between Common, runtime, and tools.
//  Details: Records the CryEngine-inspired dependency direction adopted for
//           resource contracts, VFS providers, compilers, and future renderer
//           backends without copying CryEngine's historical directory tree.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ADR 0002: Common, Runtime, And Tool Boundaries

## Status

Accepted.

## Context

CypherEngine needs one resource language and one family of serialized contracts
that can be consumed by command-line tools, future Qt tools, tests, and the
runtime. The repository also contains concrete native-filesystem behavior,
compiler implementations, resource ownership, and renderer code. Combining
these responsibilities in Common would make every consumer depend on platform
and product implementation details.

CryEngine 1 provides a useful structural lesson: its shared Common surface
exposes cross-module contracts, while concrete systems and resource-converter
implementations live in owning modules. Cypher adopts that dependency direction,
not CryEngine's exact 2002 source layout or implementation.

## Decision

`src/CypherCommon` owns:

- low-level, platform-neutral foundations
- stable IDs, handles, descriptors, callbacks, and service contracts
- CYKV syntax, schemas, validation primitives, and typed source descriptions
- explicitly serialized source/cooked format layouts
- provider-neutral VFS and stream contracts
- reusable providers only when they are broadly useful and independently linked

`src/CypherEngine` owns:

- concrete runtime subsystem implementations
- runtime mount/package providers and resource lifetime
- world, simulation, audio, physics, AI, and renderer execution
- native backend objects and runtime caches

`src/CypherTools` owns:

- ResourceCompiler process and orchestration
- type-specific importers and compiler modules
- source dependency discovery and build reports
- command-line products and future Qt applications

Dependencies point inward:

```text
CypherCommon contracts and formats
              ^
              |
      +-------+-------+
      |               |
CypherEngine       CypherTools
runtime             compilers
```

Engine and Tools may share Common. Tools do not link the Engine merely to read
files or validate data, and Common never links either product layer.

The VFS is split accordingly:

- `Cypher::VirtualFileSystem` is the provider-neutral contract and facade.
- `Cypher::VfsDirectory` is an optional loose-directory provider used by source
  tools, tests, and development hosts.
- package/mount/streaming providers remain separate and implement the same
  contract when their requirements are known.

The resource compiler is a coordinator. Every resource family contributes a
compiler descriptor behind the shared ToolFramework interface; resource-type
logic does not accumulate in the command-line front end.

An API boundary requires all of the following, not just a directory:

- public headers with stable data and operations
- a named build target
- explicit target dependencies
- private third-party and implementation types hidden from consumers
- tests that link the target as a consumer would

The verified render-asset target graph is:

```text
CommonTier0 -> CommonTier1 -> CommonTier2
                              |        |
                              v        v
                   VirtualFileSystem  RenderFormats
                              \        /
                               \      /
                        ToolFramework
                              |
              +---------------+----------------+
              |               |                |
       ShaderCompiler   TextureCompiler   MaterialCompiler
              +---------------+----------------+
                              |
                  ResourceCompilerCore -> CLI
```

Compiler modules also consume the VFS contract and render formats directly;
the diagram groups those shared inputs for readability. `VfsDirectory` is linked
only by a host that selects loose native files.

## Runtime Target Migration

The current Common and tool boundaries are real CMake libraries. The top-level
runtime executable remains too permissive because it uses a recursive source glob
and a broad include path containing nearly every subsystem directory.

Migration is incremental:

1. identify one coherent runtime owner
2. declare its public contract and private implementation files explicitly
3. create a static library target with only required include paths/dependencies
4. make tests and the executable consume that target
5. remove those sources from the monolithic runtime set

The project will not copy CryEngine's old folder tree or rewrite all runtime code
at once. The useful lesson is dependency direction and interface ownership.

## Future Qt Products

Texture and material authoring share one focused Qt 6 product named **Picasso**.
They remain separate editor-neutral workspaces internally; Qt is a shell, not
the owner of asset semantics:

```text
TextureEditorCore -> TextureCompiler + renderer preview service
MaterialEditorCore -> MaterialCompiler + renderer preview service

Picasso (Qt) -> TextureEditorCore + MaterialEditorCore
Mason -> the same editor-core libraries and preview service
```

This permits one coherent texture/material application and later Mason workspaces
without duplicating validation, cooking, undoable document operations, or preview
behavior. Neither Qt types nor widgets enter Common, render formats, resource
loaders, preview contracts, or compiler module headers.

`Cypher::RenderPreview` is the Common-side synchronous contract. A request uses
either one retained runtime resource handle or one borrowed cooked in-memory
resource, allowing Picasso to preview unsaved documents after compiling them in
memory. Output is written into caller-owned RGBA8 sRGB storage. The renderer owns
the implementation and may use software, OpenGL, or Vulkan without exposing its
native types to Picasso.

## Current Foundation Phase

No renderer backend implementation is active during this phase. Work is limited
to:

1. Common contracts and dependency boundaries
2. CYKV schemas and typed source decoders
3. versioned cooked layouts
4. type-specific compiler modules
5. ResourceCompiler discovery, validation, reporting, and reproducibility
6. malformed-input, round-trip, determinism, and process-level tests

Planned format names may be cataloged, but a binary layout is frozen only when
its compiler and first real consumer define concrete requirements.

## Renderer Sequence

Once the foundation gate is satisfied, renderer work will be performed together
and file by file in this order:

1. software renderer
2. OpenGL renderer
3. Vulkan renderer, only after the backend contract is proven

The existing GLSL `.cyshader` compiler remains a valid offline toolchain slice.
It does not make OpenGL integration the current task, and a software renderer is
not expected to execute GLSL. Backend-neutral material identity will eventually
resolve to a CPU material program or a GPU shader resource in the owning backend.

## Consequences

- Common remains reusable without becoming a container for subsystem behavior.
- Tools can run headlessly without initializing the engine runtime.
- Runtime and tools can use different VFS providers through one contract.
- Resource formats remain explicit and testable across module boundaries.
- No broad source-tree rewrite is required; existing mixed files are separated
  one verified boundary at a time.
