<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/toolchain_plan.md
//  Purpose: Documents toolchain plan.
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

# CypherEngine Toolchain Plan

CypherEngine's toolchain should follow the runtime, not lead it.

That means:

- build runtime consumption first
- prove the data shape in engine code
- then build the offline pipeline that supports it
- keep tools focused until real workflow pressure exists

## Worlds and Maps

Target:

- authored arenas
- entity metadata
- collision-ready world representation
- visibility-ready world representation
- editor-friendly source format
- cooked runtime format when needed

Recommended order:

1. define the minimum `CypherWorld` runtime data the engine needs
2. load a simple loose world/map source file
3. instantiate static objects and entity spawn data from it
4. add collision and trace data once movement needs it
5. add visibility/spatial partition data once renderer pressure needs it
6. build `CypherMapCompiler` when hand-authored source data needs cooking
7. build editor-side tooling only when the runtime world path already exists

Important rule:

- the runtime world contract comes before the editor
- the editor edits real engine data, not a disconnected fake format

## Models

Target:

- runtime model loading
- later custom Cypher model format
- later compiler/decompiler tooling

Recommended order:

1. get visible content on screen using the simplest viable path
2. define the runtime model requirements
3. design the Cypher model format
4. build runtime model loader
5. build model compiler
6. build inspector/decompiler only if it truly helps iteration

## Textures and Materials

Target:

- predictable runtime texture loading
- material definition files
- later conversion, mip, compression, and packing tools

Recommended order:

1. simple direct texture loading
2. material file conventions
3. texture conversion/mip work
4. atlas/packing tools only when they reduce pain

## Resources

Target:

- handles for shaders, meshes, textures, materials, sounds, animations, and worlds
- centralized load/unload/reload behavior
- dependency tracking
- editor/runtime hot-reload direction
- runtime-ready cooked data where source formats are expensive

Recommended order:

1. define resource handles
2. load shader resources through the resource layer
3. load mesh/texture/material resources through the resource layer
4. track dependencies
5. add hot reload once the editor or iteration flow needs it

Important rule:

- the runtime should not permanently parse heavy source formats when an offline
  compiler can produce simpler runtime data
- the resource compiler should reduce runtime transformations and small
  allocation churn

## Scripts

Target:

- gameplay bytecode generated from the Cypher script path

Recommended order:

1. `rvm` runtime
2. assembler
3. game script bytecode
4. later language compiler
5. debug symbol support

## Archives / Packaging

Target:

- packaged shipping asset archives
- later custom Cypher package format if justified
- package index data suitable for fast runtime lookup

Recommended order:

1. direct loose-file runtime loading through `CypherFileSystem`
2. define archive format only once asset pressure exists
3. build create/list/extract tooling
4. keep archive design simple and debugger-friendly

Future tools:

- `cypherpak` for create/list/extract
- package diagnostics for missing files, opened files and mounted archives

## Editor

Target:

- Qt-based all-in-one Mason editor application
- live viewport
- world/object editing
- asset browser
- inspector
- console/CVar integration
- play-in-editor direction

Recommended order:

1. finish runtime memory/filesystem/resource foundations
2. finish basic input and camera
3. load a real simple world
4. create a Qt shell
5. embed or host a runtime viewport
6. edit real `CypherWorld` data
7. add save/load and undo/redo

## Tooling Rule

Do not build a full toolchain because it sounds impressive.

Build the specific tool when:

- the runtime path exists
- the manual workflow is painful
- the tool will clearly save future development time
