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
7. preserve the completed shader, texture, and material offline slices, then add
   mesh against the first renderer's concrete geometry requirements
8. finish ResourceCompiler distribution, project discovery, and reproducible
   source-to-cooked workflows
9. audit the Common/runtime/tool boundary and freeze the minimum renderer contract
10. build the software renderer together as the first contract consumer
11. add input and camera control against that first visible runtime path
12. implement OpenGL behind the proven renderer boundary
13. drive renderer submission through generation-safe resource handles
14. design `CypherWorld` and the smallest playable map/world pipeline
15. add collision, character movement, and the first custom physics slice
16. add entity/gameplay, audio, and only the AI required by the playable loop
17. add profiling and diagnostics across the frame
18. start Mason as a small world inspection and authoring client, then grow it
    alongside runtime needs
19. design honest multiplayer/client-server shape and the Lua game-script bridge
20. add specialized compilers and Mason workspaces as their runtime consumers mature
21. consider a Vulkan renderer only after the software and OpenGL backends prove
    the shared contract
22. push toward a stable product

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

1. preserve the verified shader, texture, and material offline pipelines
2. record and enforce Common/runtime/tool target boundaries
3. extract runtime subsystem libraries incrementally from the monolithic engine target
4. preserve the implemented VFS-backed owned loaders for the three cooked asset types
5. preserve the backend-neutral preview request/output contract and implement its
   first renderer provider together
6. add the mesh slice required by the first visible renderer path
7. design and implement the software renderer together, file by file
8. design and build Picasso together only after a real renderer preview provider exists
