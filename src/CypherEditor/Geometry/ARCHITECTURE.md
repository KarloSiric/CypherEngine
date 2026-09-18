# Cypher Editor Geometry Architecture

This document is the implementation contract for `Cypher::EditorGeometry`. It
expands [ADR 0005](../../../docs/adr/0005-shared-editor-geometry-core.md) into a
module boundary and a sequence of testable delivery gates. The target is a
Qt-free, renderer-free authoring library shared by CypherTileEditor, Mason,
focused geometry tools, and the authoring side of map compilation.

The directory tree is deliberately complete enough to name long-term ownership.
It is not a claim that those modules are implemented, and it is not permission
to manufacture source-file count. A C++ source unit enters a module only with a
public contract, an invariant, and a focused test.

## Architectural decision: several source representations

Cypher does not force every authored object through one universal mesh topology.
Different workflows require different canonical data:

| Representation | Canonical source | Primary use |
|---|---|---|
| `BrushSolid` | intersection of oriented planes plus side attributes | Hammer/Quake-style convex blockout and CSG |
| `EditableMesh` | vertices, edges, half-edges, loops, faces, and shells | freeform manifold surface editing |
| `Polygon2D` | one outer contour plus zero or more holes in a plane frame | floor plans, caps, coplanar overlay, tile collision regions |
| `PatchSurface` | control points, basis/type, and evaluation parameters | curved walls, pipes, and authored curved surfaces |
| `TriangleSoup` | bounded triangles with no manifold promise | import sanitation and temporary Boolean processing |
| `CookedMesh` | immutable optimized triangles and source mappings | rendering and compiler interchange only |

A brush remains a plane set. Moving a brush face changes its plane and
reconstructs the boundary cache; it does not mutate an arbitrary half-edge mesh
and attempt to rediscover the planes. This preserves convexity, grid snapping,
brush clipping, side identity, texture locking, and stable serialization.

An editable mesh uses explicit manifold adjacency. Its initial supported domain
is an oriented polygonal two-manifold with optional boundary loops during surface
editing. A face owns one outer loop and zero or more inner loops. A mesh may own
multiple shells. Solid mesh operations require closed, watertight, consistently
oriented, non-self-intersecting shells with positive material volume.

Malformed imports and non-manifold intermediates stay in `TriangleSoup` or
neutral Exchange records until sanitation constructs a checked representation.
The authoritative half-edge structure is never weakened to make invalid input
appear valid.

Conversion between representations is explicit, transactional where it replaces
authored state, and returns complete source provenance. Converting a brush to an
editable mesh is allowed. Arbitrary mesh edits do not silently round-trip back
into the original brush.

## Core identity and storage contract

Every authored geometry element may have a document-stable 64-bit source ID.
Zero is invalid. IDs are monotonic within one document identity domain and are
never recycled. Import, duplication, clipboard insertion, and merge allocate new
IDs and publish a source-to-destination map.

Live storage uses typed `{slot, generation}` handles. A brush-side handle cannot
be passed where a mesh-face handle is required. Removing an element advances or
retires its slot generation before reuse, so stale handles fail. Persistent raw
pointers and serialized slot indices are forbidden. Short-lived pointers returned
by a checked pool lookup are invalidated according to that pool's mutation rules.

The Core contract also owns allocator and scratch-memory boundaries, status/result
vocabulary, diagnostic budgets, and hard complexity limits. Allocation failure,
limit exhaustion, invalid handles, indeterminate numerical decisions, and invalid
topology are ordinary reported outcomes. None may leave a partially published
mutation.

## Numerical kernel contract

Editor geometry uses double precision for authored coordinates and intermediate
constructions. Float conversion occurs only in checked cook products. There is no
single global geometry epsilon.

The validated policy distinguishes:

- finite coordinate magnitude;
- absolute and relative distance comparison;
- minimum edge length and minimum face area;
- angular comparison;
- face planarity and plane coplanarity;
- point/plane classification;
- snap distance and explicit weld distance;
- canonical quantization and deterministic tie-breaking;
- element, intersection-event, journal, diagnostic, recursion, and scratch limits.

Adaptive or exact predicates decide topological signs where needed. Exact signs
do not automatically make constructed intersection coordinates exact, so the
Kernel also defines controlled construction, canonicalization, provenance, and
failure rules. Generic `orient2d`, `orient3d`, `incircle`, and `insphere`
implementations belong in `Cypher::Math`; Geometry owns their representation-aware
use.

Grid quantization is applied only when an operation requests snapping. Proximity
never implies an automatic weld. Welding changes topology and is an explicit,
previewable transaction.

## Dependency layers

Dependencies flow downward. Modules on one layer may exchange neutral records but
must not form hidden ownership cycles.

```text
0  Cypher::CommonTier1 + Cypher::Math
1  Core
2  Kernel
3  Representations: Brush | Mesh | Polygon2D | Patch | TriangleSoup
4  Attributes
5  Planar + Queries + Spatial
6  Validation + Tessellation
7  Transactions + Selection + Exchange
8  Primitives + Operations/Euler + Operations
9  Repair + Modifiers + Csg + Procedural
10 Serialization + Cook
```

Important dependency rules:

- Representations own data and invariants; they do not know about tools or Qt.
- `Validation` emits bounded diagnostics and proposed repair descriptors. It does
  not mutate the object it validates.
- `Repair` applies an explicitly selected plan through `Transactions`.
- `Operations` publish created/deleted/split/merged mappings and attribute
  provenance. They never update a GUI selection directly.
- `Selection` contains geometry-component set/query logic. The host owns global
  selection across geometry, entities, materials, and scene objects.
- `Tessellation` derives triangles with source mapping; it never replaces the
  source representation.
- `Csg` consumes validated inputs and publishes a checked representation or a
  failure with witnesses. It has separate brush and mesh entry paths.
- `Serialization` persists source representations and stable IDs, never live
  handles or renderer resources.
- `Cook` consumes immutable validated snapshots and cannot mutate the document.

Every directory has a README that records narrower ownership, exclusions, and its
first acceptance gate.

## Operation model

All public mutation follows one atomic path:

```text
preflight
  -> reserve output and scratch budgets
  -> begin preview transaction
  -> apply/update operation
  -> validate affected region
  -> commit(delta + remap + provenance)
       or rollback to the exact prior state
```

Interactive dragging repeatedly updates preview state and produces one undo entry
when committed. Cancel restores the original revision. If an update becomes
invalid, the host may display diagnostics while retaining the last valid preview,
but invalid topology is never published as committed state.

The operation families are:

```text
Transform   translate, rotate, scale, shear, reflect, pivot transform
Cutting     clip by plane, slice, bisect, trim
Topology    split, collapse, weld, dissolve, merge, separate, bridge, fill, flip
Modeling    extrude, inset, bevel, chamfer, knife, loop cut, solidify, sweep,
            lathe, loft
Conversion  brush-to-mesh, face-to-patch, polygon extrusion, triangulation,
            coplanar merge
```

Small Euler operations state strict preconditions and postconditions. Higher-level
operations compose those primitives rather than rewriting adjacency independently.
Modifiers such as mirror, arrays, bend, taper, and sweep-along-path retain editable
parameters until the host explicitly collapses them into a representation.

## Attribute propagation

Attributes use explicit schemas and domains. UVs, split normals, and tangents are
usually corner data; materials and smoothing groups are face or brush-side data;
crease weights are edge data. Each topology-changing operation declares:

- which source elements created each output element;
- interpolation rules for new points and corners;
- material policy for generated faces;
- world-locked versus geometry-locked texture behavior;
- hard-edge, smoothing, and crease propagation;
- conflict behavior when differently attributed regions merge.

Attribute transfer cannot be an afterthought in CSG. Newly exposed difference
faces receive an explicit cut-face policy, while retained regions preserve
operand provenance.

## Validation and repair

Structural validation checks handle generation, ownership, cycles, reciprocal
adjacency, edge incidence, source-ID uniqueness, and representation invariants.
Geometric validation checks finite coordinates, short edges, small areas,
non-planarity, duplicate or collinear points, self-intersection, winding,
T-junctions, and zero or inverted volume. Solid validation adds closed boundaries,
orientation, connected shells, containment, convex brush planes, and contradictory
or redundant planes.

A diagnostic contains a stable code, severity, module, source elements, and
optional geometric witnesses. Human-readable wording belongs to presentation.
Diagnostics are bounded by policy.

Repair is separate and undoable. Open loops are not silently fan-capped; concave,
holed, and non-planar boundaries require a checked repair choice. T-junctions and
non-manifold edge incidence remain distinct diagnostic categories.

## CSG contract

The full regularized Boolean pipeline is explicit:

```text
input normalization and validation
  -> broad-phase candidate generation
  -> robust predicate evaluation
  -> controlled intersection construction
  -> corefinement / face splitting
  -> coplanar 2D arrangement
  -> region or cell classification
  -> Boolean expression evaluation
  -> oriented boundary extraction
  -> reconstruction and stitching
  -> attribute transfer
  -> regularized cleanup and validation
  -> source-element remapping
```

Regularized results remove isolated vertices, dangling edges, and zero-volume
pieces. Public operations include union, intersection, difference, symmetric
difference, clip, and slice.

`BrushBoolean` comes first because plane-defined convex brushes provide useful
TileEditor and Mason blockout operations much sooner. `MeshBoolean` later accepts
validated arbitrary closed meshes and requires the Planar arrangement,
corefinement, classification, and reconstruction corpus. It is not implemented as
one enormous function and it does not reuse brush-specific assumptions.

The adversarial corpus includes disjoint, contained, touching at a point or edge,
shared-face, partial-coplanar, coincident, rotated, sliver, nested-shell, multiple
component, and operand-order cases. An algorithm that cannot make a supported
robust decision fails with witnesses rather than changing tolerance mid-operation.

## Cook contract

Cooked output is immutable, disposable, revisioned, and reproducible from source.
The geometry library may publish several neutral projections:

- render mesh vertices, indices, seams, material batches, and bounds;
- static collision input geometry for the physics cooker;
- visibility/occlusion surface and portal candidates;
- navigation surface candidates;
- lightmap/chart source surfaces;
- deterministic static batch and spatial chunk proposals;
- complete cooked-triangle-to-source mappings;
- versioned compiler-interchange snapshots and content hashes.

This does not move physics, navigation, visibility, or lighting policy into the
geometry core. Those peer systems decide shape decomposition, agent walkability,
PVS construction, and lighting behavior. Geometry supplies validated,
source-mapped inputs.

## TileEditor and Mason boundary

CypherTileEditor keeps its current grid-backed `.cymap` domain. Its future adapter
belongs under:

```text
src/CypherTools/CypherTileEditor/Core/Geometry/
  CypherTileGeometryAdapter.*
  CypherTileGeometryCommands.*
  CypherTileGeometrySelection.*
  CypherTileGeometrySerialization.*
```

The adapter converts a cell, wall, door, stair, or room piece into brush or mesh
source, maps picked components back to TileEditor object IDs, wraps geometry
deltas in the editor-wide history, and publishes immutable preview/cook snapshots.
Normal tile editing keeps the grid as source of truth. Conversion to freeform
geometry is explicit. `.cymap` version 3 retains its existing meaning until a
future schema deliberately adds arbitrary geometry.

Mason later receives its own document adapter and links the same
`Cypher::EditorGeometry` target. Nothing is copied out of TileEditor.

The following remain outside this library:

- Qt widgets, themes, panels, icons, menus, hotkeys, layouts, and settings;
- fly/orbit camera input, pointer capture, viewport focus, drag sessions, numeric
  input, and transform-gizmo rendering;
- scene hierarchy, workplanes, object transforms, layers, prefabs, entities,
  triggers, lights, scripted connections, and global command history;
- material/texture asset databases, thumbnails, and browser UI;
- `.cymap` tile sets, grid types, layers, painting tools, terrain rules, and cell
  metadata;
- renderer resources, physics simulation/decomposition, navmesh generation,
  lighting, PVS, acoustics, gameplay, autosave, build orchestration, and live IPC.

Those systems consume stable IDs, deltas, immutable snapshots, source mappings,
or neutral interchange. They never become geometry dependencies.

## Delivery gates

Development proceeds through complete vertical slices.

### Gate 0: foundational Core

- source-ID allocation;
- typed source and live-handle vocabulary for every representation;
- numerical and complexity policy;
- result/diagnostic and allocator/scratch contracts;
- typed generation pools with allocation-failure and stale-handle tests.

### Gate 1: plane-defined convex brush

- brush and side storage;
- six-plane box generation and reconstruction;
- convexity, boundedness, orientation, and structural validation;
- deterministic side/face traversal and source provenance.

### Gate 2: brush tessellation and cook

- the box becomes exactly 12 outward triangles;
- source-side mapping, material/UV seams, bounds, and content hash;
- repeated and cross-configuration determinism tests.

### Gate 3: first preview transaction

- drag one brush side by updating its plane;
- reconstruct and validate on every preview update;
- preserve world-locked texture projection;
- commit one invertible delta or cancel exactly;
- invalid/degenerate updates leave committed state unchanged.

### Gate 4: TileEditor vertical slice

- create a brush from one selected tile element;
- render, component-pick, transform, undo, and rebuild it through the adapter;
- preserve the existing grid workflow and `.cymap` version 3 semantics.

### Gate 5: editable mesh foundation

- generation pools for vertex, half-edge, edge, loop, face, shell, and mesh;
- checked builder and deterministic traversal;
- canonical box acceptance: 8 vertices, 12 edges, 24 half-edges, 6 loops,
  6 faces, one shell, Euler characteristic 2, outward orientation, positive
  signed volume, and valid twin/next/face/origin relationships.

### Gate 6: atomic mesh operations

- move vertex, edge split, face split, edge collapse, explicit weld, and dissolve;
- exact undo/redo, remap, provenance, attribute propagation, and local BVH update;
- then extrude, plane cut/cap, inset, bridge, chamfer, bevel, and solidify.

### Gate 7: planar and CSG prerequisites

- polygons with holes, arrangements, overlay, constrained triangulation;
- self-intersection and closed-solid queries;
- explicit repair plans and adversarial/fuzz budgets.

### Gate 8: Booleans and extended procedural geometry

- brush regularized Booleans, followed later by mesh Booleans;
- curves, sweeps, patches, subdivision, and displacement as isolated families;
- serialization, migrations, extended cook projections, and golden corpora.

## Test architecture

Tests mirror behavior rather than directory count:

```text
tests/CypherEditor/Geometry/
  Core/             identity, pools, policy, budgets, allocation failure
  Kernel/           predicate and construction adversarial cases
  Representations/  brush, mesh, polygon, patch, and soup invariants
  Validation/       malformed fixtures and exact diagnostics
  Transactions/     preview, rollback, undo/redo, remap, provenance
  Operations/       preconditions, topology, attributes, inverse edits
  Csg/              canonical pairs, coplanarity, regularization, provenance
  Spatial/          refit/rebuild equivalence, picking, snapping candidates
  Serialization/    round-trip, migration, corrupt-input budgets, determinism
  Cook/             triangle/source maps, seams, hashes, snapshots
  Fuzz/             parser, builder, operations, overlay, Boolean sequences
  Golden/           reviewed source and cooked neutral fixtures
  Benchmarks/       large edits, BVH updates, CSG stages, cook throughput
```

Every mutating test validates before and after, checks the exact affected region,
applies the inverse delta, and compares restored authored state. Fuzzers enforce
time, element, diagnostic, and scratch limits. Golden data never stores compiler
object layouts or pointer values.

## Completion standard

A line-count target cannot prove geometry correctness. This core is ready for
Mason when TileEditor has exercised the same public library through real create,
inspect, select, preview, transform, undo, serialize, tessellate, cook, and brush
CSG workflows; malformed and adversarial inputs fail without corrupting state;
and deterministic output is verified on supported platforms. Mason then consumes
the tested library rather than receiving a copied fork.
