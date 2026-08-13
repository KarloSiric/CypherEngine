<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherResource/README.md
//  Purpose: Records the implementation boundary of the runtime resource layer.
//  Details: This note distinguishes the completed synchronous ownership slice
//           from later loader, dependency, streaming, and hot-reload work.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherResource Runtime

`CypherResource` is the engine-owned asset lifetime layer between normalized
VFS paths and subsystem-specific payloads.

## Current Slice

The first synchronous slice provides:

- stable resource IDs derived from normalized virtual paths and type IDs
- generation-safe 64-bit resource handles with a 16/32/16
  slot/generation/type layout
- fixed-capacity, allocator-backed manager tables
- allocation-free open-addressed lookup after manager initialization, with
  probe-cluster repair on removal so long sessions do not accumulate tombstones
- resource-type loader registration and compact runtime type slots
- cache identity and reference-counted acquire/retain/release operations
- transactional failed-load rollback
- recursive dependency-cycle detection during synchronous callbacks
- deterministic reverse-load-order shutdown
- diagnostics, state inspection, statistics, tests, and benchmarks
- separately linked VFS-backed `CYSH`, `CYTX`, and `CYMT` loader adapters that
  own complete cooked blobs while publishing validated zero-copy views

The manager is owner-thread only. Resource callbacks own decoding and creation
of their type-specific payloads. The resource manager owns identity and lifetime;
it does not own filesystem path resolution, format parsing, GPU calls, or audio
backend calls.

## File Responsibilities

- `CypherResource_Types.h`: public states, configuration, callbacks, and stats
- `CypherResource.h`: public manager operations
- `CypherResource_Internal.h/.cpp`: private tables, lookup, lists, and handles
- `CypherResource.cpp`: manager initialization, shutdown, and error names
- `CypherResource_Registry.cpp`: loader-type registration
- `CypherResource_Access.cpp`: load, cache, reference, and payload access paths
- `CypherResource_RenderAssets.h/.cpp`: optional cooked render-resource adapters;
  this target links VFS and render formats without coupling the generic manager

## Planned Growth

Add only when a real resource type exercises the requirement:

1. explicit dependency edges retained by parent resources
2. renderer-native shader/texture/material object creation above cooked payloads
3. mesh loader after the first concrete mesh format exists
4. asynchronous request queue and completion publication
5. hot-reload invalidation and dependency propagation
6. reload-safe payload replacement and deferred backend destruction
7. budgets, residency, eviction, streaming, and detailed diagnostics

Those features should become focused files or libraries as their contracts become
real. Empty per-resource wrappers are not useful architecture.
