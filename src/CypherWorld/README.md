<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/README.md
//  Purpose: Defines the ownership and staged layout of the world runtime.
//  Details: CypherWorld owns loaded spatial world state and prepares visible,
//           renderer-neutral submissions without owning graphics API objects.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherWorld

`CypherWorld` is the runtime spatial database for a loaded map. It knows which
world objects exist, where their bounds are, which terrain and environment data
is resident, and which objects are relevant to a particular view.

It does not issue OpenGL, software-renderer, or Vulkan commands. It emits
backend-neutral candidates that `CypherRender` validates, sorts, batches, and
draws.

## Ownership

CypherWorld owns:

- loaded map instances and world metadata
- static placements and runtime render proxies
- world transforms, bounds, layers, cells, and spatial indexes
- coarse CPU visibility, distance/LOD selection, and portal traversal
- terrain runtime state and terrain residency decisions
- environment state such as sky, fog, water, wind, and vegetation placement
- renderer-neutral visible-object, light, and environment submissions

CypherWorld does not own:

- gameplay identity or component storage; that belongs to `CypherEntity`
- rigid-body simulation or collision broadphase; that belongs to `CypherPhysics`
- asset bytes or resource lifetime; that belongs to `CypherResource`
- GPU objects, render passes, draw sorting, or presentation; that belongs to
  `CypherRender`
- editable mesh topology, selection, undo, or terrain sculpt history; that
  belongs to Mason and the map-authoring layer

## Dependency Rule

The renderer must never reach into CypherWorld to discover a scene. Host asks
CypherWorld to build a view submission, then passes that immutable submission to
CypherRender. This keeps OpenGL, software, and Vulkan backends independent from
the world representation.

## Current Status

This directory is an architecture scaffold. No world API is advertised as
implemented yet. Public types and operations will be introduced one gate at a
time, with tests and an explicit CMake target when the first executable world
code is added.

See `docs/world_runtime_module_map.md` and
`docs/adr/0004-world-renderer-ownership.md` for the complete staged plan.
