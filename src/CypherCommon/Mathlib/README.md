<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/README.md
//  Purpose: Records what CypherMath covers, what it deliberately does not,
//           and why, so gaps are visible decisions rather than oversights.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherMath

## Owns

Representation-independent numerical operations: scalars, vectors, matrices,
rotations, transforms, bounding volumes, intersection queries, planar and
polygonal geometry, clipping, projection, quantization, and exact geometric
predicates. No allocation, no global state, no hidden entropy, no I/O.

## Does not own

Topology, documents, selection, undo, renderer resources, or scene state.
Editable topology belongs to `Cypher::EditorGeometry`; cooked runtime data
belongs to `CypherWorld`. Cryptographic randomness belongs to `CypherSecurity` —
`CypherMath_Random` is explicitly not suitable for it.

## Precision model

Two parallel surfaces, deliberately:

- **f32 runtime** (`vec3_t`, `plane_t`, `aabb_t`, ...) for the render, culling
  and simulation path.
- **f64 authoring** (`vec3d_t`, `planed_t`, `aabbd_t`, ...) for Mason and
  `CypherEditorGeometry`, which author in binary64 and convert to f32 only at
  explicit, checked cook/runtime boundaries.

Not every type has both. An f64 counterpart exists where an authoring consumer
needs it and is deliberately absent otherwise — see *Deferred* below.

Functions split into **checked** (`Try*`, validates and reports failure) and
**raw** (`constexpr`, trusts its documented preconditions). Raw functions are
the hot path; the caller or a higher layer such as
`CypherGeometry_Kernel_Classification` is responsible for having validated the
input.

## Covered

| Area | Files | Notes |
|---|---|---|
| Scalars, angles | `Scalar`, `Angle` | f32 + f64 |
| Vectors | `Vector2/3/4` | f64 for 2 and 3 only |
| Matrices, rotations | `Matrix3/4`, `Quaternion` | slerp, nlerp |
| Transforms | `Affine3`, `Transform` | `affine3d_t`; TRS decompose via `Transform_TryFromAffine3` |
| Bounding volumes | `Bounds`, `Sphere` | `aabbd_t` |
| Planes, rays, triangles | `Plane`, `Ray`, `Triangle` | `planed_t`, `segmentd_t`, `triangle3d_t` |
| Intersection | `Intersection`, `Frustum` | ray/AABB/sphere/triangle, line-plane, three-plane |
| Planar geometry | `Geometry2D`, `Polygon` | area, centroid, containment, simplicity, convexity, ear-clip triangulation |
| Brush construction | `Brush`, `Clip` | plane-set boundary recovery, Sutherland-Hodgman |
| Exact predicates | `Predicates`, `Expansion` | `Orient2D`/`Orient3D`, `InCircle`/`InSphere`, filtered fast path + exact fallback |
| Projection, editor aids | `Viewport`, `Gizmo`, `Snap`, `UV` | |
| Compression | `Quantization`, `FixedPoint` | snorm/unorm/range/angle/quat-smallest-three, 16.16 |
| Curves | `Spline` | bezier, hermite, catmull-rom, arc-length tables |
| Numerics | `Numerics` | closest segment points, semi-implicit and angular integration, quadratic solve |
| Randomness | `Random` | PCG32, deterministic, seeded, streamed |
| Batch | `Batch` | SoA `vec3_soa4_t`, `f32_soa4_t` |

## Not covered, and why

Nothing below is an oversight. Each is absent because no consumer exists yet,
and the engine rule is that a feature justifies itself through a real need. The
shape of an API designed without a caller is a guess, and guesses in this layer
are expensive to unwind once several gates depend on them.

### Physics — `src/CypherPhysics` is an empty stub

Absent: OBB, capsule and cylinder primitives, convex hull, inertia tensors and
mass properties, GJK/EPA support functions, sweep and continuous collision,
contact manifold generation, constraint Jacobians, restitution and friction.

The GJK support-function signature in particular is determined by the broadphase
and collider representation, neither of which has been designed. Writing it now
would be guessing at both.

### Renderer — shading-adjacent math

Absent: half-float (f16) conversion, sRGB/linear conversion (only a format
*enum tag* exists in `RenderSystem`), spherical harmonics.

`CypherRender` is real and populated but has not needed these yet. Add them when
it does.

### Animation — `src/CypherAnimation` is an empty stub

Absent: dual quaternions for skinning, squad interpolation.

### General utilities

Absent: noise (Perlin/simplex — will be wanted by `CypherWorld/Terrain`),
easing functions, eigen/covariance decomposition.

Eigen/covariance is the one worth noting: it is a single primitive with at least
three eventual consumers (OBB fitting, inertia tensors, point-cloud alignment).
It is still deferred because all three of those consumers are themselves
deferred, but it is the natural first addition once any of them lands.

### Deferred f64 counterparts

`vec4d_t`, `mat3d_t`, `mat4d_t`, `quatd_t`, `transformd_t`, `rayd_t`. Authoring
uses affine transforms and planes, not projective matrices or runtime rays, so
these have no consumer. `Affine3d_TryInverse` avoids needing `mat3d_t` by
inverting through cross products.

## Known constraints

- `Brushd_BuildVertices` enumerates plane triples and tests each candidate
  against every plane, so it is **quartic in plane count**. It targets
  authoring-scale brushes; `geometry_limit_policy_t::cBrushSidesPerBrushMax` is
  capped at 256 for this reason. Raising that cap requires a different
  construction algorithm, not just a larger number.
- `Intersection_TryThreePlanesD`'s `minimumAbsDeterminant` is only a meaningful
  conditioning threshold for **unit-length** normals, because the determinant
  scales with the product of the three normal lengths. Only finiteness is
  validated at runtime.
- `CypherMath_Expansion.cpp` and `CypherMath_Predicates.cpp` compile with
  `-ffp-contract=off`. The FMA inside `TwoProduct` is deliberate and must remain
  the only one; automatic contraction elsewhere would break the exactness that
  `TwoSum`/`TwoDiff` depend on.
- `CypherMath_Random` is deterministic by contract. Sequences are reproducible
  from `(seed, stream)` across platforms and build configurations, and must stay
  that way — changing the algorithm changes every replay and every seeded test.

## Third-party provenance

`CypherMath_Random` implements PCG32 (XSH-RR) as published by Melissa O'Neill
(2014), written from the algorithm description rather than copied from the
reference implementation. Verified bit-exact against the reference output vector
for `(state = 42, seq = 54)`; that known-answer test is in
`CypherCommon_Mathlib_Random_Tests.cpp` and must keep passing.

`CypherMath_Expansion` and `CypherMath_Predicates` implement the classic
non-adaptive exact-predicate construction described by Shewchuk (1997), derived
here from the published technique rather than adapted from his source. The
circumcircle/circumsphere fallbacks expand the standard lifted determinant; a
fixed radix-2 superaccumulator covers finite inputs whose exponent span cannot
fit in binary64 expansion components. Review licensing before importing any
third-party predicate code.
