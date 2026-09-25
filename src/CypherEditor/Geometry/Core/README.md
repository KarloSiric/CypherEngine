# Core

Identity, handles, policy, diagnostics and scratch budgets, plus the numeric kernel every representation builds on: exact predicates, coordinate keys, convex hulls, the 2D planar toolkit, attribute schemas and the editable spatial index.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_Attributes_BrushSideStore` | Bounded, failure-atomic storage for brush-side attributes |
| `CypherGeometry_Attributes_MeshStore` | Sidecar attribute storage for an EditableMesh: per-corner UVs and colour, per-face material and smoothing groups, per-edge surface flags |
| `CypherGeometry_Attributes_Schema` | Attribute domains, material references, and the brush-side attribute record |
| `CypherGeometry_Diagnostics` | Bounded structured diagnostics for editable geometry |
| `CypherGeometry_IdAllocator` | Monotonic persistent identity allocation for geometry |
| `CypherGeometry_Kernel_Classification` | Policy-aware point/plane orientation classification |
| `CypherGeometry_Kernel_ConvexHull` | 3D convex hull construction from a point cloud |
| `CypherGeometry_Kernel_CoordinateKey` | Canonical coordinate quantization and total ordering |
| `CypherGeometry_Kernel_TriangleIntersection` | Exact segment/triangle and triangle/triangle intersection *tests* built only from exact Orient2D/Orient3D |
| `CypherGeometry_OperationContext` | Borrowed services shared by one geometry operation |
| `CypherGeometry_Planar_Frame` | The orthonormal plane frame used to move geometry between 3D authoring space and a 2D planar working space |
| `CypherGeometry_Planar_Offset` | Polyline stroking and region offsetting (inset and outset) in a plane frame |
| `CypherGeometry_Planar_Overlay` | Regularized 2D Boolean overlay of two PlanarRegions: union, intersection, difference, and symmetric difference |
| `CypherGeometry_Planar_Segment` | Exact 2D segment-pair classification and a controlled (non-exact) intersection-point construction |
| `CypherGeometry_Planar_Triangulate` | Triangulation of PlanarRegion polygons with holes |
| `CypherGeometry_Policy` | Numerical and complexity policy for authored geometry |
| `CypherGeometry_Scratch` | Bounded temporary storage for editor-geometry operations |
| `CypherGeometry_SourceIdRegistry` | Document-local persistent geometry identity ownership |
| `CypherGeometry_SpatialIndex` | A flat AABB broad-phase spatial index for brushes |
| `CypherGeometry_Types` | Identity, representation, handle, and result vocabulary for editable geometry |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Core

#### Owns

Shared identity, document-local source-ID registration and deterministic transfer
remapping, persistent-source taxonomy, representation-qualified aliases over the
Common wide generation pool, bounded structured diagnostics, allocation
boundaries, scratch budgets, and hard complexity limits.

#### Does not own

Geometric predicates, topology storage, documents, UI commands, or renderer state.

#### First acceptance gate

Allocate and claim persistent source IDs monotonically, preserve retired claims,
restore them only through an explicit undo path, remap transferred IDs
deterministically, reject invalid policies, report diagnostics without hidden
allocation, and prove that bounded scratch and Common pool growth are
failure-atomic while stale live handles cannot resolve after removal, clear,
slot reuse, or terminal-generation retirement.

### Kernel

#### Owns

The Geometry-facing numerical contract used by every authored representation:

- double-precision authoring coordinates and planes;
- explicit coordinate-range and finite-value checks;
- filtered predicates with an exact-sign fallback where a wrong sign can alter
  topology;
- checked plane classification and controlled geometric constructions;
- deterministic coordinate quantization and total ordering;
- the small policy facade that converts Geometry tolerances into an explicit
  operation decision.

The primitive vector, plane, and predicate implementations belong in
`Cypher::Math` when they are useful outside the editor. This module owns how the
Geometry runtime applies those primitives. It must never hide a topology choice
behind an undocumented global epsilon.

#### Does not own

Topology ownership, document state, repair, user-facing snapping, gizmos, or
tool behavior. `Constraints/` owns snapping and constraint solving. A
representation operation owns the decision it makes from a predicate result.

#### Floating-point contract

Predicate translation units require strict floating-point behavior: no
fast-math, reassociation, or implicit contraction that can change a sign. The
supported environment assumes round-to-nearest and gradual underflow. Any
third-party exact-predicate algorithm must record its source and license before
code is adapted.

#### First implementation slices

1. Add `vec2d_t` and `vec3d_t` beside the existing float vector APIs in
   `Cypher::Math`, with finite checks and checked length/normalization.
2. Add `plane3d_t` and checked construction/classification.
3. Add orientation predicates and the Geometry policy facade.
4. Add authoring-coordinate quantization as a separate reviewed slice.

Each slice lands with focused contract tests before a dependent Geometry type
is allowed to use it.

#### First acceptance gate

Classify orientation and plane sidedness deterministically at documented scale
limits without a global epsilon. Boundary tests must include `nextafter`
neighbors, reversed winding, very small and very large finite inputs, NaN and
infinity rejection, and repeatability across Debug, Release, and sanitizer
builds.

### Planar

#### Owns

Plane projection, segment arrangements, polygon overlay, offsetting, holes, constrained triangulation, and source mapping for 2D regions.

#### Does not own

Persistent 3D representation ownership or arbitrary screen-space UI polygons.

#### First acceptance gate

Overlay two coplanar polygons with holes deterministically and map every output contour segment to its inputs.

### Spatial

#### Owns

Source/component bounds, editable BVH, local refit/rebuild, component picking,
overlap, snapping candidates, and dirty-region tracking. Derived triangle pick
caches are admitted only after the corresponding Tessellation contract exists;
they retain source mapping and snapshot revision.

#### Does not own

A second octree merely for feature parity, renderer acceleration structures, or input routing.

#### First acceptance gate

Match brute-force query results after local edits, refits, rebuilds, and snapshot publication.

### Attributes

#### Owns

Two dependency levels with an explicit boundary:

- `Schema` owns typed domains, descriptors, values, opaque material references,
  UV projection records, and bounded representation storage;
- `Propagation` owns operation-specific interpolation, generated-face policy,
  texture locking, normals/tangents, smoothing, creases, conflicts, and
  provenance.

#### Does not own

Material asset loading, shader binding, or texture-browser UI.

#### First acceptance gate

Validate deterministic brush-side schema/storage first, then preserve material
and world-locked texture projection through a transactional brush-side drag and
deterministic tessellation.

### Attributes / Schema

#### Owns

Typed attribute descriptors, domains, opaque material references, UV projection
records, value/storage validation, and bounded storage used by canonical source
representations. Domains include representation root, vertex, corner, edge,
face, brush side, control point, and sample where supported.

#### Does not own

Topology-changing propagation, interpolation across edits, material databases,
texture loading, shader binding, or browser UI.

#### First acceptance gate

Allocate, copy, query, and validate bounded brush-side material and UV projection
records with deterministic enumeration and failure-atomic growth.
