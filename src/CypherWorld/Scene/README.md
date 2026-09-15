<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Scene/README.md
//  Purpose: Defines runtime scene and object ownership inside CypherWorld.
//
//////////////////////////////////////////////////////////////////////////
-->

# Scene

This folder will own loaded world-object records, generation-checked handles,
transforms, hierarchy links, bounds, layer masks, cells, and render proxies.

A world object is a spatial runtime record, not necessarily a gameplay entity.
One entity may own several render proxies, and static map geometry may have no
entity at all. GPU buffers and materials are referenced by renderer/resource
handles; CypherWorld does not own their native storage.

First implementation: a bounded object table with add, remove, lookup, transform
update, bounds update, and deterministic iteration.
