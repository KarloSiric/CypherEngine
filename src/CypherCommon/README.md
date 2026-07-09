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
