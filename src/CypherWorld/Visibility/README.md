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

First implementation: deterministic linear frustum culling with explicit layer
and distance masks. Portals and occlusion are later gates.
