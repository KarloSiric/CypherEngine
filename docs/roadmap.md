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
9. complete the shader, texture, material, and mesh vertical asset slices
10. drive real renderer submission through resource handles
11. design `CypherWorld` and the smallest playable map/world pipeline
12. add collision, character movement, and the first custom physics slice
13. add entity/gameplay, audio, and only the AI required by the playable loop
14. add profiling and diagnostics across the frame
15. start Mason as a small world inspection and authoring client, then grow it
    alongside runtime needs
16. design honest multiplayer/client-server shape and the Lua game-script bridge
17. add specialized compilers and Mason workspaces as their runtime consumers mature
18. consider a Vulkan renderer only after the backend-neutral contract is proven
19. push toward a stable product

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

1. build the development shader cooker on the finished source/cooked contracts
2. load one cooked shader through VFS and `CypherResource`
3. audit the Common renderer boundary and design the OpenGL integration together
4. replace raw renderer shader ownership with generation-safe handles
5. add texture and material only after the shader integration is tested
6. build input, camera control, and a visible playable renderer loop
