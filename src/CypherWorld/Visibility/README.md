<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Visibility/README.md
//  Purpose: Defines coarse CPU visibility ownership inside CypherWorld.
//
//////////////////////////////////////////////////////////////////////////
-->

# Visibility

CypherWorld visibility answers which world records are plausible rendering
candidates for one view. The staged query is:

```text
active layers
  -> connected area/portal set
  -> spatial candidates
  -> frustum test
  -> distance and LOD policy
  -> residency check
  -> renderer-neutral submission
```

The renderer may later perform finer GPU occlusion or draw compaction. It does
not repeat world traversal or own the area graph.

Visibility is not simulation activation. An object outside the camera can still
need AI, networking, animation, audio, or physics updates. Those policies remain
with their owning systems and are coordinated separately.

The first implementation is a deterministic linear scan of the bounded object
table. It tests any matching layer bit, intersects each caller-maintained
world-space AABB with a validated frustum, and applies an inclusive distance
limit to the closest point on the bounds. It copies accepted records in slot
order. Portals, LOD, residency, occlusion, and spatial acceleration are later
gates.
