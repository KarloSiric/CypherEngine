<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Environment/README.md
//  Purpose: Defines world environment state and spatial placement ownership.
//
//////////////////////////////////////////////////////////////////////////
-->

# Environment

This folder will own world-level sky, fog, weather, water-volume, wind,
vegetation-placement, decal-placement, and light-placement state.

Ownership is split by concern: CypherWorld selects and submits relevant
environment records; CypherRender implements their visual techniques;
CypherPhysics implements physical water or collision behavior; CypherAudio uses
area and environment information for ambience and acoustics.

Vegetation placement, clustering, distance classes, and billboard/impostor
selection policy belong here. Vertex deformation and final shading belong to
the renderer.
