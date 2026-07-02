<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/roadmap.md
//  Purpose: Documents roadmap.
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

# CypherEngine Roadmap

This roadmap is the high-level summary version of [development_phases.md](development_phases.md).
For the full dated schedule, LOC ranges, subsystem acceptance criteria, and editor/toolchain plan, read [master_plan.md](master_plan.md).

`CypherEngine` is the engine runtime inside the CypherEngine project.

## The order that matters

1. finish the engine foundation and Common correctness
2. finish the command/cvar/cfg backbone
3. finish memory arenas, pools, and allocation diagnostics
4. strengthen the virtual filesystem and package diagnostics
5. build `CypherResource` as the asset lifetime layer
6. add simple streaming/resource completion flow above VFS
7. keep `SDL3`/`OpenGL` runtime bootstrap stable
8. add input and camera control
9. add materials, textures, meshes, and real renderer submission through handles
10. design `CypherWorld` and the first custom map/world source format
11. add profiling and diagnostics across the frame
12. add custom tools and formats for models, materials, packages, maps, and resources
13. design honest multiplayer/client-server shape
14. add VM/game-script path
15. grow the Qt editor into the long-term Mason editor application
16. push toward a stable product

## Scope reminder

REAP is not just:

- a shooter

It is also:

- an engine runtime
- a VM
- a tools pipeline
- a data/format ecosystem
- an editor/runtime workflow

That means progress must be staged carefully.

## Practical solo rule

At any given time:

- one active milestone
- one active subsystem focus
- one or two active files

That rule matters more than ambition.

## Current near-term protocol

Near-term work should follow this sequence:

1. complete `CypherCommon_String` correctness
2. add tests and benchmarks for each completed Common group
3. keep scalar implementations correct and measured
4. design `CypherCommon_Simd` after scalar String is stable
5. return to Resource ownership before growing renderer/world/editor scale
