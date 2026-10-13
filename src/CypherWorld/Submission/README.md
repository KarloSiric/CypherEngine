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

The first executable slice stores copied visible object records in a bounded,
backend-neutral array. `WorldSubmission_Init` allocates storage once;
`WorldSubmission_Build` copies one `World_QueryView` result without a frame
allocation, and a generation-checked token permits value reads until
`WorldSubmission_End`. A failed or oversized build never publishes a partial
view. The World may be destroyed after the copy; the storage owner must remain
alive through all token use. Concurrent mutation or use is not supported.

Future submissions will also need lights, decals, terrain chunks, particles,
and environment constants.

A submission contains logical slots, stable IDs, and plain values. It never
contains an OpenGL name, Vulkan handle, native pointer, renderer command
buffer, or mutable world record. The cube sample currently resolves logical
slots itself and issues OpenGL draws; a production CypherRender consumer that
classifies passes,
resolves renderer handles, sorts, batches, and records commands is planned.

This storage owns its copies rather than borrowing frame-arena memory. Its
token lifetime is explicit and ends when the host calls
`WorldSubmission_End` after its renderer frame. Reusing the opaque storage
pointer after shutdown is invalid even with an old token.
