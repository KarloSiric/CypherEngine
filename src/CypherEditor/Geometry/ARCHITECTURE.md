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
| `PlanarRegion` | one or more polygons with holes in a shared plane frame | floor plans, caps, profiles, and tile collision regions |
| `PatchSurface` | control points, basis/type, and evaluation parameters | curved walls, pipes, and authored curved surfaces |
| `CurveNetwork` | retained curves, connectivity, basis-specific control data, and parameter domains | paths, sweeps, lofts, roads, rails, and pipes |
| `HeightField` | bounded height samples, tiles, and hole masks | terrain-like authoring and deterministic surface generation |

Neutral processing and derived output stay outside that source taxonomy:

| Processing/output form | Contract | Primary use |
|---|---|---|
| `TriangleSoup` | bounded triangles with no manifold promise | import sanitation and temporary Boolean processing |
| `PolygonSoup` | bounded faces without authoritative adjacency | neutral exchange and repair input |
| stage records | intersection graphs, arrangements, corefined meshes, cells, and boundary fragments | private checked CSG stages |
| cooked products | immutable optimized data plus source mappings | rendering, collision, navigation, visibility, lighting, and compiler interchange |

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
never recycled. A document records both every identity claimed during its domain
lifetime and the currently live subset. Loading has one explicit registration
phase; after it is sealed, undo may reactivate only a previously claimed retired
identity. Import, duplication, clipboard insertion, and merge allocate new IDs
and publish a source-to-destination map. Serialization persists the allocator's
next/high-water value even when the object owning the highest issued ID has been
deleted; rebuilding that value from live objects would permit identity reuse.

Live storage uses typed `{slot, generation}` handles. A brush-side handle cannot
be passed where a mesh-face handle is required, and a mesh vertex is a different
C++ type from a planar vertex. Pools are owned by one geometry document. Compact
handles deliberately omit a document ID and may only be resolved against the
pool that issued them; cross-document operations use source IDs and remap tables.
Removing an element advances or retires its slot generation before reuse, so
stale handles fail without generation wraparound. Persistent raw pointers and
serialized slot indices are forbidden. Short-lived pointers returned by a
checked pool lookup are invalidated according to the Common Tier1 generation
pool's mutation rules. Mutable documents have one writer; immutable revisioned
snapshots are the cross-thread boundary.

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
1  Core identity, results, limits, diagnostics, scratch, operation context
2  Kernel + low-level Attributes schema/storage
3  Source representations + Intermediates + pure Primitives
4  geometry-local Document store + immutable Snapshot
5  Transactions + typed ChangeSet/remap/provenance
6  Queries + Planar
7  Validation + Tessellation
8  Selection + Exchange/Sanitation + Spatial + Constraints
9  Attribute propagation + Operations/Euler
10 leaf Operations + pure Brush/Mesh CSG + Procedural evaluators + Repair plans
11 composed Modeling/Conversion + Repair application + optional Modifiers
12 Serialization + Cook
```

Important dependency rules:

- Representations own data and invariants; they do not know about tools or Qt.
- Low-level `Attributes` defines schemas, domains, and storage used by source
  representations. Propagation/evaluation sits above topology-changing edits.
- Low-level `Primitives` use only Kernel and checked representation builders to
  generate source inputs without mutating a document. Architectural creation
  commands that require clipping or CSG live above those algorithms.
- `Document` owns geometry representation pools, identity, revisions, and
  immutable snapshots. It is not the Mason or TileEditor scene document.
- `Validation` emits bounded diagnostics and stable repair hints. It does not
  construct Repair-owned plans or mutate the object it validates.
- `Repair` translates diagnostics into an explicitly selected plan and applies
  it through `Transactions`.
- `Operations` publish created/deleted/split/merged mappings and attribute
  provenance. They never update a GUI selection directly.
- `Selection` contains geometry-component set/query logic. The host owns global
  selection across geometry, entities, materials, and scene objects.
- `Spatial` publishes bounds indices and derived pick proxies after the required
  representation/tessellation data exists. It does not own input routing.
- `Constraints` deterministically resolves grid, angle, and component snap
  candidates. The host owns active modes, workplanes, and gestures.
- `Exchange/Sanitation` orchestrates bounded neutral input: Intermediates own raw
  records, Validation reports faults, and checked builders publish sources.
- `Tessellation` derives triangles with source mapping; it never replaces the
  source representation.
- `Csg` consumes validated inputs and publishes a checked representation or a
  failure with witnesses. It has separate brush and mesh entry paths.
- `Serialization` persists source representations and stable IDs, never live
  handles or renderer resources.
- `Cook` consumes immutable validated snapshots, tracks source/policy dependency
  keys, and cannot mutate the document.

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
Transient evaluation followed by an explicit transactional bake is the default
for mirror, arrays, bend, taper, and sweep-along-path. A modifier retains editable
parameters only after a real workflow justifies an approved persistent recipe
source kind with versioned serialization and collapse provenance.

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
geometry is explicit and creates a Mason `.cyscene`. `.cymap` version 3 retains
its tile-grid meaning; it will not become Mason's arbitrary scene document.

Mason later receives its own document adapter and links the same
`Cypher::EditorGeometry` target. Nothing is copied out of TileEditor.

The following remain outside this library:

- Qt widgets, themes, panels, icons, menus, hotkeys, layouts, and settings;
- fly/orbit camera input, pointer capture, viewport focus, drag sessions, numeric
  input, and transform-gizmo rendering;
- scene hierarchy, workplanes, object transforms, layers, prefabs, entities,
  triggers, lights, scripted connections, and global command history;
- material/texture asset databases, thumbnails, and browser UI;
- `.cymap` tile sets, grid types, layers, painting tools, terrain-domain rules,
  foliage/biomes, and cell metadata;
- renderer resources, physics simulation/decomposition, navmesh generation,
  lighting, PVS, acoustics, gameplay, autosave, build orchestration, and live IPC.

Those systems consume stable IDs, deltas, immutable snapshots, source mappings,
or neutral interchange. They never become geometry dependencies.

## Delivery gates

Development proceeds through the measurable vertical slices in
[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md). The dependency order is:

```text
Core closeout
  -> double-precision Kernel and attribute schema
  -> plane-defined Brush + box primitive
  -> geometry Document/Snapshot/Transactions
  -> Queries/Tessellation/Spatial/Selection/Constraints
  -> brush edits and early Brush CSG
  -> first Serialization/Cook/TileEditor slice
  -> EditableMesh topology and Euler edits
  -> Planar/Sanitation/Modeling/Repair
  -> staged Mesh CSG
  -> extended source/procedural families
  -> incremental peer-facing Cook
```

Each gate defines an observable completion criterion, malformed and budget
cases, required provenance, and the exact point where tests, golden fixtures,
fuzzing, or benchmarks become meaningful. A gate does not close because its
directory exists or a happy-path generator produces visible output.

## Test architecture

Tests mirror behavior rather than directory count:

```text
tests/CypherEditor/Geometry/
  Core/             identity, pools, policy, budgets, allocation failure
  Kernel/           predicate and construction adversarial cases
  Representations/  canonical brush, mesh, planar, patch, curve, and heightfield invariants
  Intermediates/    polygon/triangle soup validation, sanitation, and provenance
  Validation/       malformed fixtures and exact diagnostics
  Transactions/     preview, rollback, undo/redo, remap, provenance
  Operations/       preconditions, topology, attributes, inverse edits
  Csg/              canonical pairs, coplanarity, regularization, provenance
  Spatial/          refit/rebuild equivalence, picking, snapping candidates
  Serialization/    round-trip, migration, corrupt-input budgets, determinism
  Cook/             triangle/source maps, seams, hashes, snapshots
  Fuzz/             parser, builder, operations, overlay, Boolean sequences
  Golden/           reviewed source and cooked neutral fixtures
benchmarks/CypherEditor/Geometry/
  measured large edits, BVH updates, CSG stages, and cook throughput
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
