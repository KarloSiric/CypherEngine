<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Streaming/README.md
//  Purpose: Defines cell residency and world streaming coordination.
//
//////////////////////////////////////////////////////////////////////////
-->

# Streaming

CypherWorld decides which map cells and terrain chunks should become resident
based on views, player locations, budgets, and explicit preload regions.

It does not read files directly or own decoded assets. It sends asynchronous
requests to CypherResource, tracks request generations and cancellation, and
activates a cell only after every required runtime dependency is ready.

First implementation uses a fully resident map. Streaming is introduced only
after loading, visibility, and resource lifetime are correct without it.
