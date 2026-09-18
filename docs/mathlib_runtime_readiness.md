<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/mathlib_runtime_readiness.md
//  Purpose: Defines CypherMath conventions, verified readiness, and expansion gates.
//  Details: Records the numerical contract required by rendering, World, movement,
//           projectiles, collision, physics, and authoring tools.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherMath Runtime Readiness

## Status

`src/CypherCommon/Mathlib` is the canonical math implementation and the
`Cypher::Math` target is the canonical build boundary.

The core algebra is suitable for current renderer cameras, transforms, basic
world visibility, editor grids, picking with finite projections, and simple
projectile calculations. It does not need a rewrite or a folder move.

It is not yet a complete character-collision library, rigid-body library,
large-world representation, production CSG kernel, or cross-platform bitwise
deterministic simulation layer. Those capabilities require explicit work in
their owning subsystems.

## Audited Surface

The September 18, 2026 audit covered:

- 30 implementation files;
- 32 public/focused headers;
- 14 inline implementation headers;
- 13,482 implementation and header lines;
- 16 focused test translation units with 3,853 lines;
- 4 benchmark translation units with 860 lines;
- the current renderer examples and TileEditor consumers;
- Debug, Release benchmark, and ASan/UBSan executions.

The module families are:

```text
Core numeric
    Scalar, Angle, Vector2/3/4, Matrix3/4, Quaternion

Transforms
    Affine3, Transform

Spatial primitives and queries
    Plane, Ray, Bounds, Sphere, Triangle, Frustum, Intersection, Batch

Authoring geometry
    Geometry2D, Polygon, Brush, Clip, Gizmo, Snap, Spline, UV, Viewport

Representation and stability
    FixedPoint, Quantization, Numerics
```

This breadth is real and tested, but most runtime use still comes from examples
and TileEditor. The first `CypherWorld` slice must prove the same contracts in a
shipping-style data flow.

## Normative Coordinate Contract

All runtime systems must use the same conventions. Conversion belongs at an
importer, backend, or external-library boundary.

### World axes

```text
+X = forward
+Y = left
+Z = up
-Y = right
```

The coordinate system is right-handed. `cross(a, b)` is the standard
right-handed cross product.

### Camera axes

View space is right-handed and looks along local `-Z`. Camera up is local `+Y`.
Camera/right helpers must respect the world `+Y = left` convention rather than
silently importing another engine's basis.

### Matrices

- Storage is column-major: `m[column * 4 + row]` for `mat4_t`.
- Vectors are column vectors.
- `Mat4_Multiply(a, b)` applies `b` first and then `a`.
- Translation lives in the fourth column.
- `Affine3` stores three linear basis columns followed by translation.

Storage order, mathematical convention, and graphics-API upload layout are
separate concepts. Backend upload code must set its transpose policy explicitly.

### Quaternions

- Component order is `x, y, z, w`.
- The vector part is `x/y/z`; `w` is the scalar part.
- Rotations are active and right-handed.
- `Quat_Multiply(a, b)` applies `b` first and then `a`.
- Operations with a `Unit` suffix require a normalized quaternion.

### Transforms

A decomposed transform evaluates as:

```text
worldPoint = translation + rotation * (scale * localPoint)
```

Equivalent matrix order is `T * R * S`.

Composing rotated nonuniform scales may create shear, so exact composition
returns `affine3_t`. A decomposed `transform_t` must not pretend that arbitrary
affine transforms remain translation/rotation/scale-only.

### Angles and time

`angle_t` stores radians. Callers use explicit degree/radian constructors and
conversions. Public operations must not accept an unlabeled scalar whose unit is
ambiguous.

Simulation time units belong to Host/Physics policy. Mathlib does not infer
seconds, milliseconds, frames, or ticks from a scalar.

### Projection and depth

Perspective and orthographic constructors are right-handed. The clip-depth
range is explicit:

- `NEGATIVE_ONE_TO_ONE` for the conventional OpenGL range;
- `ZERO_TO_ONE` for Vulkan/Direct3D-style depth.

Viewport screen depth is normalized to `[0, 1]` regardless of clip convention.
Viewport origin is explicit as top-left or bottom-left.

Finite view-projection matrices compose with the current frustum extraction,
corner reconstruction, and picking-ray APIs. Infinite perspective matrices do
not have finite far corners or a finite far plane. Until Mathlib introduces an
explicit five-plane/infinite representation, checked frustum and picking APIs
must reject that combination deterministically and document the limitation.

Reversed-Z, asymmetric/off-center projection, temporal jitter, clip-Y policy,
depth linearization, and position reconstruction remain renderer-driven future
work.

### Planes and rays

A plane uses:

```text
dot(normal, point) + d = 0
```

Signed distance is metric only when the normal is unit length. Checked
construction or deserialization must validate this before sphere/frustum tests
use radii in world units.

Ray directions do not have to be unit length. The ray parameter `t` therefore
uses the caller's direction scale unless a specific operation documents metric
distance.

### GPU packing

`vec3_t` is a tightly packed 12-byte CPU type with scalar alignment. It is not a
`std140` `vec3`. Renderer uniform/storage-buffer layouts use explicit packed
records or copy components into API-specific alignment. Native CypherMath
structs must never be uploaded blindly merely because the field names match a
shader.

## Tolerance Policy

There is no universal engine epsilon. This is deliberate.

Each checked operation takes a tolerance or minimum magnitude whose unit and
meaning belong to that operation. Valid tolerances must be finite and
nonnegative. NaN or infinity is invalid input and must never turn a rejected
geometry case into success.

Owning systems define named policies:

| Owner | Example tolerances |
| --- | --- |
| Render | homogeneous `w`, inverse pivot, frustum-plane normalization |
| World | bounds inflation, visibility boundary, cell/local-coordinate conversion |
| Physics | contact slop, separation, ground angle, sweep skin, depenetration cap |
| Editor geometry | weld distance, plane classification, collinearity, minimum area, grid snap |
| Asset import | source-unit conversion, transform decomposition, normal/tangent validation |

Absolute singularity thresholds are scale-sensitive. Matrix, brush, and
authoring callers must choose them from the scale of their data; a literal that
works for a one-meter object is not automatically correct for a kilometer map
or a micrometer import.

## Validity And Trust Boundaries

Finiteness and semantic validity are different questions.

- `Transform_IsFinite` means every component is finite.
- A valid transform additionally needs a unit rotation and an allowed scale.
- `Plane_IsFinite` means coefficients are finite.
- A valid metric plane additionally needs a nondegenerate or unit normal.
- `Frustum_IsFinite` means all stored plane coefficients are finite.
- A valid classification frustum needs normalized inward planes.

Fast query loops may consume already validated data. Source documents, cooked
resources, editor state, network input, and externally constructed records are
trust boundaries and require checked validation before entering those loops.

Spline arc tables are another trust boundary. A valid table has finite entries,
nondecreasing distance and parameter values, parameters within `[0, 1]`, and a
usable final distance. The checked lookup validates all samples and is therefore
`O(n)` before its binary search. A table successfully built by Mathlib or
validated once at load time may use the explicitly unchecked `O(log n)` lookup;
untrusted serialized data must never enter that path directly.

## Readiness By Consumer

| Consumer | Readiness | What is usable now | Required next work |
| --- | --- | --- | --- |
| finite renderer camera | good foundation | view, finite perspective/orthographic projection, clip ranges, viewport mapping, picking, frusta | integrate in renderer-owned camera path; add reversed-Z/jitter only when required |
| object transforms | good foundation | vectors, quaternions, TRS, exact affine composition, inversion/decomposition | checked asset/world transform validation and GPU packing adapters |
| World Gate 1 visibility | ready for reference path | AABB, sphere, planes, finite frusta, transforms, intersection classification | generational object table, linear query, tolerance policy, immutable submission |
| simple projectiles | partial | rays, triangles, planes, spheres, closest/simple intersections | swept shapes, time of impact, rich hit data, and Physics ownership |
| character movement | not ready | vector and plane primitives only | capsule, sweeps, multi-plane clip/slide, steps, ground classification/snap |
| collision/rigid physics | not ready | basic primitives and queries | OBB/capsule/convex support, SAT or GJK/EPA, manifolds, inertia, broadphase |
| TileEditor | broad early foundation | grids, viewport, rays, polygons, splines, brush construction, clipping, UV, gizmos | converge duplicate picking code; scale/degeneracy fixtures; stronger topology ownership |
| Mason topology/CSG | foundation only | convex helper operations and planar calculations | topology kernel, holes/arrangements, adaptive predicates, transactional operations |
| deterministic multiplayer | limited | fixed-point/quantization tools and deterministic control-flow intent | explicit fixed-tick policy and per-operation guarantees; float paths are not bitwise portable |

## Physics Ownership

Mathlib should provide representation-independent primitives and geometric
queries that several systems can reuse. It should not become the physics engine.

Likely shared additions, driven by the first collision slice:

- capsule and OBB records;
- closest point for segment/triangle and related primitive pairs;
- ray/segment tests against capsule and OBB;
- swept sphere, capsule, and AABB queries;
- time-of-impact records containing fraction/time, point, normal, start-solid,
  all-solid, and penetration information;
- convex-shape support functions if more than Physics needs them.

`CypherPhysics` owns:

- collision-world storage and broadphase;
- character capsule sweeps, multi-plane slide, step up/down, and grounding;
- contacts, constraints, manifolds, bodies, mass, and inertia;
- fixed simulation step and event ordering;
- debug query capture and physical-world diagnostics.

World render visibility and Physics broadphase remain separate even when both
use AABBs.

## Authoring Geometry Ownership

The current Brush, Polygon, Clip, UV, Snap, and Gizmo modules are useful
authoring helpers. They are not a complete mesh/topology kernel.

Mason needs explicit topology, transactional mutations, stable element IDs,
polygon holes/arrangements, robust orientation predicates, validation, and
repair policy in `CypherEditorGeometry`. Mathlib supplies numerical primitives;
it does not own editable mesh history or silently repair invalid topology.

Convex brush construction enumerates plane triples, tests candidate points
against every plane, and searches existing vertices for duplicates. Its cost can
approach `O(P^4 + V^2)`. It is appropriate for small bounded convex brushes.
Mason must enforce plane/vertex caps and benchmark scaling and degeneracy before
using it as a general CSG foundation.

## Large-World Decision

Public world positions are currently single-precision floats. A float has about
seven decimal digits of precision. If one unit equals one meter, centimeter
resolution degrades at distances on the order of 100 km.

Before cooked world serialization and spatial indices are frozen, choose one:

1. double-precision world positions with float-local rendering and physics;
2. cell/region coordinates plus float-local positions;
3. origin rebasing with a documented maximum playable extent.

This decision belongs to World. Mechanically changing every Mathlib type to
double would increase storage/bandwidth without defining streaming, rendering,
physics, or network behavior.

## Determinism Contract

The phrase “deterministic CPU-side math” must be qualified.

CypherMath currently provides:

- explicit operation order and control flow;
- repeatability expected for one pinned compiler, target, flags, and CPU path;
- fixed-point and quantization modules for deliberately constrained data.

It does not promise cross-architecture bitwise identity for ordinary floats.
Standard-library trigonometry, fused multiply-add availability, and different
SSE2/NEON instruction sequences can produce different low bits. Multiplayer or
replay code must specify which state requires bitwise identity and choose fixed,
quantized, or reconciliation-based behavior accordingly.

## SIMD Policy

`Batch` provides compile-time scalar, SSE2, and NEON four-lane paths. There is no
runtime CPU dispatch. That is an acceptable initial policy.

On the audited Apple M1 host, the SIMD batch transform was approximately
16-21% faster than the scalar fixture. This supports retaining the optimized
path, not converting every primitive to SIMD. Scalar code remains the
correctness reference, and additional SIMD work requires a real renderer,
visibility, animation, or physics profile.

## Verification Results

The final hardened tree passed the complete focused suite in both ordinary
Debug and sanitizer configurations. The explicit target extraction also passed
the complete repository Debug suite:

```text
Full Debug tree:       248/248 tests passed
Focused Debug:          16/16 Mathlib tests passed in 4.58 seconds
Focused ASan/UBSan:     16/16 Mathlib tests passed in 5.96 seconds
```

Representative post-hardening Release benchmark results on the audited Apple
M1 host:

| Operation | Approximate time or throughput |
| --- | ---: |
| vector dot/cross | 8.59 ns |
| quaternion rotate | 11.7 ns |
| matrix 4x4 multiply | 3.44 ns |
| matrix 4x4 inverse | 86.7 ns |
| transformed AABB | 7.24 ns |
| ray/AABB | 16.0 ns |
| scalar batch transform | 2.08-2.10 billion items/s |
| SIMD batch transform | 2.43-2.55 billion items/s |
| polygon triangulation fixture | 143 ns |
| cube brush build | 158 ns |
| picking ray | 66.0 ns |
| quaternion slerp | 70.7 ns |
| finite frustum/AABB | 31.4 ns |
| spline arc table, 17/65/257 samples | 66 / 256 / 1,027 ns |

Validation is performed once at each public authoring boundary; composed
polygon and brush algorithms then use private prevalidated helpers. This keeps
the malformed-input checks without repeating full scans inside nested loops.
macOS did not expose CPU frequency to Google Benchmark and thread affinity could
not be fixed. These numbers are a local regression baseline, not a
cross-machine performance claim.

Current benchmarks favor small successful inputs. Future suites must include
scale growth, degeneracy, rejection paths, poor conditioning, and cache pressure.

## Next Implementation Order

This pass completed the finite-input and semantic-validity gate and retained
regression tests for NaN, infinity, malformed tables, nonunit transforms, and
degenerate planes/frusta. The remaining order is:

1. Keep the infinite-projection limitation explicit until an optional far plane,
   far directions, and infinite picking contract are designed together.
2. Integrate existing transforms, bounds, frusta, and intersections in
   `CypherWorld` Gate 1.
3. Define named Renderer, World, Physics, and Editor tolerance policies.
4. Decide the large-world position model before freezing world formats.
5. Add capsule/OBB and sweep primitives only with the first movement/collision
   vertical slice.
6. Add reversed-Z, off-center projection, jitter, and depth reconstruction with
   the renderer feature that consumes them.
7. Keep topology, CSG, character movement, solvers, and broadphase structures in
   their owning modules.

## Related Documents

- [Runtime subsystem structure](adr/0006-runtime-subsystem-structure.md)
- [World runtime module map](world_runtime_module_map.md)
- [World and renderer ownership](adr/0004-world-renderer-ownership.md)
- [Shared editor geometry boundary](adr/0005-shared-editor-geometry-core.md)
- [Renderer host module map](renderer_host_module_map.md)
