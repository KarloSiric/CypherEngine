<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Spatial/README.md
//  Purpose: Defines spatial indexing and query ownership inside CypherWorld.
//
//////////////////////////////////////////////////////////////////////////
-->

# Spatial

This folder will own acceleration structures used to find world objects by
bounds, point, volume, ray, cell, and view.

Cypher will not force every workload through one octree. The expected split is:

- cooked BVH or cell index for mostly static map objects
- dynamic AABB tree or loose octree for moving render proxies
- quadtree for terrain chunks
- area/portal graph for connected indoor visibility

Physics owns its own collision broadphase. Reusing one mutable tree for both
render visibility and physical simulation would couple different update rates,
query semantics, and threading requirements.

First implementation: linear bounds queries that establish correctness and a
benchmark baseline before an acceleration structure is selected.
