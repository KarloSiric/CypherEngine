<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/adr/0004-world-renderer-ownership.md
//  Purpose: Defines ownership between world, renderer, and adjacent systems.
//  Details: Prevents spatial-world behavior, renderer commands, simulation, and
//           editor state from becoming one inseparable scene subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ADR 0004: World And Renderer Ownership

## Status

Accepted for staged implementation.

## Context

Cypher needs to support indoor authored spaces, outdoor heightfield terrain,
static and dynamic objects, vegetation, portals, multiple renderer backends,
and a future Mason editor. Treating all of this as renderer code would make the
graphics backend responsible for map representation, streaming, visibility,
and editor concerns. Treating it all as one generic scene graph would similarly
mix unrelated lifetimes and query workloads.

CryEngine 1 is a useful architectural reference because its `Cry3DEngine`
module managed world and visibility concerns separately from `RenderDll` and
its API-specific renderer implementations. The historical implementation still
made direct renderer calls from the 3D engine in several paths. Cypher keeps the
ownership lesson while using a narrower, renderer-neutral handoff.

The published line-count estimates for old engines are not used as design
requirements. Repository structure, interfaces, data flow, tests, and measured
game workloads are authoritative.

## Decision

The runtime module is named `CypherWorld`. It is not named `Cypher3DEngine`
because that phrase is easily confused with the complete engine or renderer.

### Ownership Table

| Module | Owns | Does Not Own |
| --- | --- | --- |
| `CypherHost` | Process startup, shutdown, and frame sequencing | World data, renderer objects, or OS implementations |
| `CypherSystem` | OS events, windows, graphics-context integration, clocks, and platform services | World visibility or rendering policy |
| `CypherFileSystem` | Mounts, canonical virtual paths, and byte I/O | Resource decoding or world residency policy |
| `CypherResource` | Cooked asset records, dependency lifetime, loading, and CPU payloads | World placement or GPU command execution |
| `CypherEntity` | Gameplay identity, components, entity lifetime, and simulation-facing state | Spatial visibility acceleration or static map ownership |
| `CypherPhysics` | Collision shapes, physical broadphase, contacts, constraints, and traces | Render visibility trees or GPU objects |
| `CypherWorld` | Loaded map state, spatial records, bounds, coarse visibility, terrain, portals, environment, and streaming decisions | Gameplay logic, physical simulation, native graphics objects, or editor history |
| `CypherRender` | GPU objects, render passes, queues, sorting, batching, fine/GPU culling, backend commands, and presentation | Map traversal, terrain source data, portal graphs, or resource file loading |
| Mason | Editable topology, heightmap layers, selection, undo, authoring views, and source documents | Shipping runtime structures or graphics API objects |
| `CypherSceneCompiler` | Conversion of Mason `.cyscene` source into validated cooked world payloads | Runtime object lifetime or live editor state |

### Dependency Direction

```text
Mason -> CYKV .cyscene -> CypherSceneCompiler -> .cyscene_c
                                                |
                                                v
Host -> CypherResource -> CypherWorld -> view submission -> CypherRender
  |                         |                         |             |
  v                         v                         v             v
CypherSystem             CypherEntity             frame data   OpenGL/Software/Vulkan
                            |
                            v
                       CypherPhysics
```

The diagram shows ownership flow, not a requirement that every call is made
directly. In particular:

- CypherRender must not call into CypherWorld to discover what to draw.
- CypherWorld may reference stable resource and renderer handles, but not native
  API objects.
- CypherPhysics maintains a separate broadphase from render visibility.
- Host sequences synchronization between entity, physics, world, and renderer.

### Frame Handoff

For each rendered view, Host performs this order:

1. Finish the simulation step that owns entity and physics state.
2. Synchronize changed spatial proxies into CypherWorld.
3. Ask CypherWorld to build an immutable view submission.
4. Let CypherWorld apply layers, connected areas, spatial lookup, frustum tests,
   distance/LOD policy, and residency checks.
5. Pass visible candidates, lights, terrain chunks, and environment values to
   CypherRender.
6. Let CypherRender validate handles, classify passes, sort, batch, perform
   optional fine/GPU occlusion, and record backend commands.
7. Let the selected backend execute commands and present through CypherSystem.

The initial implementation may build only the primary camera view. Shadow,
reflection, probe, and editor views will later use the same explicit query
contract rather than allowing the renderer to traverse the world itself.

### Spatial Structures

Cypher does not commit to one universal octree. Different data requires
different update and query behavior:

| Workload | Initial Method | Expected Mature Method |
| --- | --- | --- |
| Small correctness scenes | Linear object-table scan | Retained as reference path |
| Mostly static map objects | Linear scan | Cooked BVH and/or cell index |
| Moving render proxies | Linear scan | Dynamic AABB tree or measured loose octree |
| Heightfield terrain | Visible chunk scan | Terrain quadtree with neighbor-aware LOD |
| Indoor visibility | No portal optimization | Area graph and bounded portal traversal |
| Physical collision | Physics-owned structures | Physics broadphase and cooked collision trees |

An optimized structure is accepted only after it matches the reference query
and improves representative benchmarks.

### Visibility Is Not Simulation

A camera-invisible object may still require server logic, AI, physics,
animation, networking, or audio. CypherWorld visibility only decides render
relevance. Simulation activation and update-frequency policy remain explicit in
the owning systems.

### Authoring And Runtime Geometry

Mason operates on editable faces, meshes, terrain layers, optional CSG
operations, and undoable commands. CypherSceneCompiler validates and bakes that
state into runtime geometry, collision data, terrain chunks, spatial indexes,
portal data, dependencies, and spawn records.

CypherWorld never carries Mason's editable topology or undo history. Optional
brush/CSG workflows are authoring operations; they are compiled into runtime
triangle meshes and collision payloads.

## Consequences

- OpenGL, software, and Vulkan backends can consume the same submissions.
- World representation can evolve without exposing graphics API objects.
- Mason can use the runtime for preview without becoming part of shipping world
  data.
- Physics and visibility may use structures appropriate to their own workloads.
- More explicit synchronization is required between Entity, Physics, World,
  Resource, and Renderer.
- Frame-submission memory lifetime and handle validation must be documented and
  tested.

## References Studied

- OpenFarCry1 CryEngine 1 source tree: https://github.com/OpenFarCry1/Far-Cry-1-CryEngine1
- CRYENGINE RenderNode interface: https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1605658
- CRYENGINE VisArea and portal documentation: https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048727
- CRYENGINE terrain editor documentation: https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/35849146
- CRYENGINE vegetation editor documentation: https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/36865590

These references are used for architectural study only. No CryEngine source is
copied into Cypher, and third-party licensing must be reviewed before adapting
any implementation.
