<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Submission/README.md
//  Purpose: Defines the immutable CypherWorld-to-CypherRender handoff.
//
//////////////////////////////////////////////////////////////////////////
-->

# Submission

This folder will build immutable, backend-neutral view submissions containing
visible render candidates, lights, decals, terrain chunks, particles, and
environment constants.

A submission contains stable handles and plain values. It never contains an
OpenGL name, Vulkan handle, native pointer, renderer command buffer, or mutable
world record. CypherRender classifies the candidates into passes, resolves live
renderer handles, sorts and batches draws, and records backend commands.

Initial submissions may borrow frame-arena memory and remain valid only until
the matching frame finishes. That lifetime must be explicit in the public API.
