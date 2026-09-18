<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/world_runtime_module_map.md
//  Purpose: Defines the staged source map for the CypherWorld runtime.
//  Details: Lists intended files, ownership, implementation order, validation,
//           and the handoff to resources, physics, renderer, and Mason.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherWorld Runtime Module Map

## Status

Architecture approved; implementation not started.

This is a source-ownership and sequencing map. A listed `.cpp` file is not
created until it has executable behavior and focused tests. The final module is
expected to require roughly 45 to 70 implementation/header files, depending on
which responsibilities remain cohesive in practice. File count is not a quality
target.

## Mission

CypherWorld owns the loaded spatial representation of a map and converts that
representation into view-specific, renderer-neutral submissions. Its central
questions are:

- What world is loaded?
- Which spatial records exist and where are their bounds?
- Which cells, terrain chunks, and resources should be resident?
- Which records are relevant to this view?
- Which LOD and environment representation should be submitted?

It does not answer how a triangle is rasterized, how a rigid body collides, how
an entity thinks, or how Mason edits a face.

## Runtime Data Flow

```text
.cyscene_c and cooked dependencies
              |
              v
       CypherResource records
              |
              v
      CypherWorld loaded map
       |       |       |
       |       |       +--> terrain/environment state
       |       +----------> spatial indexes and portal graph
       +------------------> object records and render proxies
                              |
camera/view query ------------+
                              v
                   immutable view submission
                              |
                              v
                        CypherRender
                  pass classify/sort/batch/draw
```

## Planned Source Groups

### Core And Lifecycle

| Planned File | Responsibility |
| --- | --- |
| `CypherWorld_Public.h/.cpp` | Stable lifecycle, object, query, and view-submission operations. |
| `CypherWorld_Types.h` | Public IDs, handles, bounds, configuration, and plain result records. |
| `CypherWorld_Error.h/.cpp` | Stable errors and diagnostic names. |
| `CypherWorld_Config.h/.cpp` | Capacity, visibility, LOD, and residency policy validation. |
| `CypherWorld_Local.h` | Private aggregate state and already-validated helpers. |
| `CypherWorld_Runtime.cpp` | Initialization, map activation, frame boundaries, and ordered shutdown. |
| `CypherWorld_Stats.h/.cpp` | Snapshot counters without exposing mutable runtime state. |

### Loaded Map And Objects

| Planned File | Responsibility |
| --- | --- |
| `Scene/CypherWorld_Map.h/.cpp` | One active cooked map, identity, metadata, origin, and bounds. |
| `Scene/CypherWorld_Object.h/.cpp` | Generation-checked spatial object records and lookup. |
| `Scene/CypherWorld_ObjectTable.h/.cpp` | Bounded slot storage, free list, generations, and deterministic iteration. |
| `Scene/CypherWorld_Transform.h/.cpp` | World transforms, dirty propagation, and previous-frame state. |
| `Scene/CypherWorld_Hierarchy.h/.cpp` | Optional parent/child spatial attachment without gameplay ownership. |
| `Scene/CypherWorld_Bounds.h/.cpp` | Local/world bounds maintenance and validation. |
| `Scene/CypherWorld_Layer.h/.cpp` | Visibility, editor, collision-reference, and query layer masks. |
| `Scene/CypherWorld_RenderProxy.h/.cpp` | World placement plus stable mesh/material/resource references. |
| `Scene/CypherWorld_StaticGeometry.h/.cpp` | Cooked static batches and immutable map geometry records. |
| `Scene/CypherWorld_Spawn.h/.cpp` | Validated spawn records handed to Entity/game code. |

### Spatial Queries

| Planned File | Responsibility |
| --- | --- |
| `Spatial/CypherWorld_SpatialTypes.h` | Nodes, query filters, candidate spans, and diagnostics. |
| `Spatial/CypherWorld_LinearIndex.cpp` | Correctness/reference implementation used by early gates and tests. |
| `Spatial/CypherWorld_StaticBVH.h/.cpp` | Read-only cooked index for static object bounds. |
| `Spatial/CypherWorld_DynamicTree.h/.cpp` | Updates and queries for moving render proxies. |
| `Spatial/CypherWorld_CellGrid.h/.cpp` | Coarse world cells and lookup for residency and diagnostics. |
| `Spatial/CypherWorld_Query.h/.cpp` | Point, AABB, sphere, ray-candidate, and volume queries. |
| `Spatial/CypherWorld_RayQuery.h/.cpp` | World-record ray filtering; exact collision remains in Physics. |
| `Spatial/CypherWorld_SpatialValidate.h/.cpp` | Structural validation and reference-result comparison. |

### Visibility And Areas

| Planned File | Responsibility |
| --- | --- |
| `Visibility/CypherWorld_View.h/.cpp` | Immutable camera/frustum/layer/distance query description. |
| `Visibility/CypherWorld_Visibility.h/.cpp` | Orchestrates one coarse CPU visibility query. |
| `Visibility/CypherWorld_FrustumCull.cpp` | Bounds/frustum tests and culling-reason accounting. |
| `Visibility/CypherWorld_DistanceCull.cpp` | Distance classes, fade bands, and LOD policy. |
| `Visibility/CypherWorld_VisArea.h/.cpp` | Indoor/outdoor area records and camera-area membership. |
| `Visibility/CypherWorld_Portal.h/.cpp` | Directed openings, clipped portal volumes, and state. |
| `Visibility/CypherWorld_PortalTraversal.cpp` | Bounded, cycle-safe connected-area visibility traversal. |
| `Visibility/CypherWorld_Occlusion.h/.cpp` | CPU occluder decisions if measurements justify them. |
| `Visibility/CypherWorld_VisibilityStats.h/.cpp` | Candidate, accepted, and per-reason rejection counters. |

### Terrain

| Planned File | Responsibility |
| --- | --- |
| `Terrain/CypherWorld_Terrain.h/.cpp` | Active terrain dataset, dimensions, scale, and lifecycle. |
| `Terrain/CypherWorld_Heightfield.h/.cpp` | Immutable height samples and bounded sampling queries. |
| `Terrain/CypherWorld_TerrainChunk.h/.cpp` | Chunk records, bounds, neighbors, and resource references. |
| `Terrain/CypherWorld_TerrainQuadtree.h/.cpp` | Hierarchical terrain visibility and LOD candidate lookup. |
| `Terrain/CypherWorld_TerrainLOD.h/.cpp` | Screen-error selection, hysteresis, and neighbor constraints. |
| `Terrain/CypherWorld_TerrainMesh.h/.cpp` | Runtime mesh-reference selection; GPU data remains in Renderer. |
| `Terrain/CypherWorld_TerrainMaterial.h/.cpp` | Layer and material-region references. |
| `Terrain/CypherWorld_TerrainQuery.h/.cpp` | Height, normal, region, and chunk lookup. |
| `Terrain/CypherWorld_TerrainStreaming.cpp` | Terrain residency requests and activation. |

### Environment

| Planned File | Responsibility |
| --- | --- |
| `Environment/CypherWorld_Environment.h/.cpp` | Active environment profile and view constants. |
| `Environment/CypherWorld_Sky.h/.cpp` | Sky selection and world-level time/weather parameters. |
| `Environment/CypherWorld_Fog.h/.cpp` | Fog volumes and global fog records. |
| `Environment/CypherWorld_Water.h/.cpp` | Water-volume placement and renderer/physics references. |
| `Environment/CypherWorld_Vegetation.h/.cpp` | Instances, clusters, distance classes, and impostor policy. |
| `Environment/CypherWorld_Wind.h/.cpp` | World wind fields and affected-region queries. |
| `Environment/CypherWorld_Light.h/.cpp` | Spatial light records and visible-light candidates. |
| `Environment/CypherWorld_Decal.h/.cpp` | Persistent world decal placement and culling records. |

### Streaming And Residency

| Planned File | Responsibility |
| --- | --- |
| `Streaming/CypherWorld_Cell.h/.cpp` | Cooked cell metadata, bounds, dependencies, and state. |
| `Streaming/CypherWorld_Streaming.h/.cpp` | Desired-cell calculation and request orchestration. |
| `Streaming/CypherWorld_Residency.h/.cpp` | Loading, ready, active, evicting, and failed transitions. |
| `Streaming/CypherWorld_Request.h/.cpp` | Generation-safe asynchronous request tracking and cancellation. |
| `Streaming/CypherWorld_Budget.h/.cpp` | CPU/GPU/world residency budgets and eviction candidates. |
| `Streaming/CypherWorld_Preload.h/.cpp` | Spawn, cinematic, and editor-requested preload regions. |

### Renderer-Neutral Submission

| Planned File | Responsibility |
| --- | --- |
| `Submission/CypherWorld_RenderItem.h` | Plain visible surface/mesh candidate values. |
| `Submission/CypherWorld_RenderList.h/.cpp` | Frame-arena storage and deterministic candidate append. |
| `Submission/CypherWorld_LightList.h/.cpp` | Visible light candidates and influence metadata. |
| `Submission/CypherWorld_EnvironmentView.h/.cpp` | Sky, fog, water, wind, and ambient values for one view. |
| `Submission/CypherWorld_Submission.h/.cpp` | Complete immutable handoff and explicit lifetime rules. |

### Diagnostics

| Planned File | Responsibility |
| --- | --- |
| `Debug/CypherWorld_Debug.h/.cpp` | Bounds, cells, portals, terrain LOD, and culling visualization data. |
| `Debug/CypherWorld_Report.h/.cpp` | Text/structured snapshots for console, tools, and tests. |
| `Debug/CypherWorld_Validate.h/.cpp` | Expensive development-only invariant checks. |

## Cooked Map Contract

The eventual `.cyscene_c` payload should provide independently bounded sections
for at least:

- header, version, byte order, checksums, and target profile
- string and resource-reference tables
- map metadata and world bounds
- static geometry chunks and material slots
- object placements and render-proxy records
- terrain chunks, height data, LOD metadata, and material regions
- cells and static spatial-index nodes
- VisAreas, portals, and occluder metadata
- collision payload references
- entity spawn records and authored properties
- lights, decals, water, vegetation, and environment records
- dependency tables and streaming groups

Each section is validated before activation. Runtime structures do not alias
untrusted offsets without range, alignment, count, and overflow checks.

## Implementation Gates

### Gate 0: Architecture And Baseline

- Accept ADR 0004 and the ownership table.
- Keep CypherRender free of map traversal and terrain ownership.
- Record representative object-count and map-size targets.
- Add a `CypherWorld` CMake target only when Gate 1 has executable code.

### Gate 1: Small In-Memory World

- One initialized world and explicit lifecycle state.
- Generation-checked object table.
- Transform, bounds, layer, and render-proxy updates.
- Linear frustum/distance query.
- Immutable visible-item submission consumed by a renderer test double.

Exit: a hardcoded map of cubes can be moved through while stale handles,
invalid bounds, and invalid frame lifetimes are rejected.

### Gate 2: Cooked Static Map

- Versioned `.cyscene_c` header and bounded section reader.
- Static geometry, placements, dependencies, and spawn records.
- Transactional map load/activation and complete rollback on failure.
- Fully resident map; no asynchronous streaming yet.

Exit: the same test map loads from cooked data and produces deterministic view
submissions.

### Gate 3: Spatial Acceleration

- Reference linear queries remain available in tests.
- Cooked static BVH/cells match reference results.
- Dynamic tree handles moving proxies without rebuilding static data.
- Representative 10,000 and 100,000 object benchmarks justify the structures.

### Gate 4: Terrain Runtime

- Heightfield queries and chunk bounds.
- Quadtree visibility and screen-error LOD selection.
- Neighbor-safe transitions without cracks.
- Terrain submissions use renderer handles, not native API objects.

### Gate 5: VisAreas And Portals

- Indoor/outdoor area membership.
- Bounded, cycle-safe portal traversal.
- Portal clipping is tested with closed, open, nested, and cyclic layouts.
- Outdoor visibility through openings is limited to the visible portal region.

### Gate 6: Streaming

- Cells transition through explicit residency states.
- Resource requests are asynchronous, cancellable, generation-safe, and
  observable.
- Failed or late requests cannot activate stale map state.
- Budgets and eviction are tested under repeated travel and reload.

### Gate 7: Mason Runtime Bridge

- Mason can preview a cooked snapshot without sharing editable topology.
- Authoring changes produce explicit rebuild/reload transactions.
- Editor camera, selection, and debug views use the same world query contracts.
- Multi-user collaboration remains a later service, not a CypherWorld concern.

## Verification Plan

### Correctness Tests

- initialization, shutdown, repeated load, and failure rollback
- object capacity, stale generations, removal, and slot reuse
- transform/bounds changes and deterministic iteration
- layer, frustum, distance, and LOD edge cases
- reference scan versus accelerated-query equivalence
- portal traversal cycles, depth bounds, closed portals, and clipped views
- terrain sampling, chunk borders, LOD neighbor constraints, and seams
- streaming cancellation, late completion, eviction, and map reload
- immutable submission lifetime and renderer-handle validation
- malformed, truncated, overflowed, and version-mismatched cooked sections

### Benchmarks

- object insertion, removal, and transform/bounds updates
- 10,000 and 100,000 static-object view queries
- mixed static/dynamic workloads and dynamic-tree updates
- portal traversal across representative indoor graphs
- terrain quadtree and LOD selection across camera paths
- view-submission construction, sorting inputs, and frame-arena usage
- streaming request churn and residency-set calculation

Benchmark results choose optimizations; they do not replace correctness tests.

## Definition Of Done

CypherWorld is complete for the first game only when:

- the game and Mason load the same cooked world contract
- ownership and shutdown order are deterministic
- representative maps load, query, render, reload, and fail cleanly
- visibility never controls unrelated simulation by accident
- no native graphics type crosses the world boundary
- no editable Mason topology enters shipping runtime data
- tests cover every public operation and malformed-data boundary
- sanitizer, Windows, Linux, and macOS CI pass
- representative world workloads meet measured frame and memory budgets

This definition is capability-based. Reaching a particular line or file count
does not make the subsystem finished.
