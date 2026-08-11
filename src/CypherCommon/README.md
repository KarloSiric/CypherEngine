<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/README.md
//  Purpose: Documents the CypherCommon source tree.
//  Details: This directory is the shared public/common foundation for engine,
//           game, tools, editor, tests, and future asset pipeline code.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherCommon

`CypherCommon` is the shared public/common foundation for CypherEngine.

It owns the custom runtime utility layer and public subsystem contracts. It does
not own full subsystem implementations.

The rule is:

```text
Common defines shared shape.
Subsystems implement behavior.
```

Use this tree for types, handles, descriptors, interfaces, format headers,
callbacks, utility containers, string tools, hashing, parsing, serialization,
reflection metadata, and low-level runtime support.

Do not place renderer backends, physics solvers, Qt widgets, game rules, asset
cookers, audio mixers, or network replication implementation here.

## Canonical layout

Generic implementation is organized by dependency level:

```text
Tier0       portable runtime primitives
Tier1       containers, text, memory, hashing, parsing, and byte IO
Tier2       schemas, typed configuration, and shared data composition
Mathlib     reusable scalar, vector, geometry, and authoring math
Security    cryptographic contracts backed by approved libraries
```

Subsystem-facing contracts are organized by ownership:

```text
DataModel      schema, serialization, and reflection contracts
FileSystem     virtual filesystem contracts
Formats        source and cooked format declarations
AssetSystem    authored and cooked asset metadata
ResourceSystem loaded-resource lifetime contracts
RenderSystem   rendering, image, texture, and material contracts
InputSystem    device, event, action, and binding contracts
SoundSystem    sound, emitter, listener, bus, and stream contracts
ToolFramework  editor-neutral and command-line tool contracts
```

Do not recreate Tier0 or Tier1 functionality in parallel root folders. A
subsystem folder receives code only when more than one consumer needs its shared
contract.
