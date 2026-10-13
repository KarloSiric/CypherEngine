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

## Renderer direction (2026-10-13)

OpenGL 4.1 is the common first graphics backend on macOS, Windows, and Linux.
We will finish and validate its useful runtime rendering path before adding
Vulkan, DirectX 12, or Metal implementations. Higher OpenGL versions are not a
requirement for the common renderer contract. Game, asset, and editor code use
Cypher handles and material/pass descriptions rather than native OpenGL names.
A software renderer may still be useful for diagnostics, but it is no longer a
prerequisite for OpenGL work.

The current OpenGL path can draw an indexed lit cube and has bounded cooked
texture/material adoption, static mesh upload, aligned per-object uniforms,
offscreen color/depth targets, and a host-surface contract. These are component
milestones, not a complete world renderer. The next gates are a real authored
asset path, many-object scene submission with explicit visibility and pass
ordering, lighting and shadow integration, profiling, and an editor viewport
that consumes the same renderer. Each gate needs a visible sample and focused
failure and ownership tests before more backend abstractions are added.

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
10. complete OpenGL 4.1 as the first graphics backend and contract consumer
11. add input and camera control against that first visible runtime path
12. prove multi-object scene, pass, lighting, and asset flows on OpenGL
13. drive renderer submission through generation-safe resource handles
14. design `CypherWorld` and the smallest playable map/world pipeline
15. add collision, character movement, and the first custom physics slice
16. add entity/gameplay, audio, and only the AI required by the playable loop
17. add profiling and diagnostics across the frame
18. connect Mason's 3D viewport to the proven runtime renderer, then grow its
    inspection and authoring tools alongside runtime needs
19. design honest multiplayer/client-server shape and the Lua game-script bridge
20. add specialized compilers and Mason workspaces as their runtime consumers mature
21. add Vulkan, DirectX 12, and Metal backends after OpenGL proves the shared
    contract and the target platforms justify each implementation
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
5. keep the backend-neutral preview request/output contract while proving a
   native OpenGL 4.1 renderer path
6. connect validated mesh, material, texture, and per-object uniform ownership
   into a many-object runtime scene
7. add visible pass, lighting, and shadow behavior one measured slice at a time
8. move Mason's 3D viewport onto that renderer only when host-surface and
   resource-lifetime behavior are demonstrably stable
