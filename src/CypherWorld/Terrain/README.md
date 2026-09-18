<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Terrain/README.md
//  Purpose: Defines heightfield and terrain runtime ownership.
//
//////////////////////////////////////////////////////////////////////////
-->

# Terrain

This folder will own immutable cooked heightfield data, terrain chunks, quadtree
nodes, LOD selection, neighbor constraints, material-region references, height
queries, and terrain residency decisions.

Mason owns sculpting tools, editable layers, undo history, and source-document
state in `.cyscene`. CypherSceneCompiler converts that authoring state into
chunked `.cyscene_c` terrain data and precomputed LOD metadata. CypherRender owns
the resulting GPU buffers and terrain pipelines.

The runtime must prevent cracks between adjacent LODs, keep height/collision
queries consistent with rendered terrain, and make streaming transitions
observable through diagnostics.

First implementation begins only after one cooked static world can load and
submit visible objects correctly.
