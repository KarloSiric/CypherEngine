<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEditor/Geometry/IMPLEMENTATION_PLAN.md
//  Purpose: Defines the dependency-ordered delivery plan for editor geometry.
//  Details: Each gate is a complete vertical slice with measurable correctness,
//           determinism, budget, integration, and performance requirements.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Editor Geometry Implementation Plan

This plan turns the long-term contracts in [ARCHITECTURE.md](ARCHITECTURE.md)
into reviewable implementation slices. `Cypher::EditorGeometry` remains a
Qt-free, renderer-free library shared by CypherTileEditor, Mason, compiler
tools, and focused geometry utilities.

The plan measures behavior and invariants rather than source-file or line count.
A source file enters the build only when its public contract, failure behavior,
and focused tests exist.

## Current state

Gate 0 began in commit `090d2b71` and currently provides:

- stable source identity and never-reuse registration;
- representation-qualified source references and handle vocabulary;
- wide tagged generation pools with permanent generation retirement;
- bounded structured diagnostics;
- bounded local-first operation scratch memory;
- numerical and complexity policy validation;
- Debug, Release, and sanitizer contract coverage.

Gate 1 added binary64 math, exact orientation wrappers, canonical coordinate
keys, policy-aware classification, and the brush-side attribute schema.

Gate 2 is closed. `BrushSolid` stores planes with persistent side identity and
rejects duplicate side IDs; boundary reconstruction is failure-atomic and
canonical (coordinate-key vertex order, sorted edges, face rings rotated to
their smallest vertex), so every side-order permutation yields the same
traversal; the box generator commits source IDs only after the brush is built;
deep validation checks Euler, edge sharing, Newell winding, face area, and
edge length. Unbounded, redundant, contradictory, out-of-range, and
over-limit plane sets fail without publishing a boundary.

Every module outside `Core` is still a design contract unless its source is
explicitly listed in `src/CypherEditor/Geometry/CMakeLists.txt`.

## Working method

Implementation proceeds one behavior slice at a time:

1. State the public contract, ownership, preconditions, postconditions, limits,
   deterministic ordering, and failure atomicity.
2. Add focused unit and bounded deterministic property cases. A commit never
   leaves the normal test configuration intentionally red.
3. Implement the smallest code path that satisfies the contract.
4. Test normal, boundary, malformed, allocation-failure, and cancellation paths
   that exist at that layer.
5. Run the focused Debug test, the Geometry contract suite, ASan/UBSan, and the
   relevant Release target.
6. Add a benchmark only when the slice contains a real measured hot path or a
   complexity claim. Benchmarks do not replace correctness tests.
7. Review the staged diff for dependency direction, hidden allocation, unstable
   iteration, lossy precision, missing provenance, and host/editor leakage.
8. Commit the closed slice before starting the next behavior.

A snippet should normally introduce one public behavior and its tests. A large
algorithm is decomposed into checked stages with neutral records; it is not
submitted as one opaque function.

## Locked architecture decisions

### Mixed canonical source representations

Plane-defined `BrushSolid` and topology-defined `EditableMesh` are both
canonical sources. Planar regions, patches, curve networks, and height fields
remain distinct sources because their authoring invariants differ.

Triangle and polygon soups are bounded intermediates. Cooked triangles are
immutable derived products. Neither becomes the universal editable model.

### Primitive retention

The first box, wedge, prism, stair, arch, and similar descriptors are transient
pure generator inputs that publish Brush, Mesh, PlanarRegion, or Patch source.
CypherTileEditor may retain its own tile/piece parameters and regenerate through
the shared generator.

A persistent `ProceduralRecipe` source kind is added only when a real Mason
workflow proves that parameter editing must survive serialization independently
of its evaluated geometry. Such a kind requires stable identity, versioned
parameters, deterministic evaluation, transaction behavior, serialization,
collapse provenance, and complete validation; it is not implied merely because
a creation dialog has parameters.

### Identity boundary

Geometry source IDs identify geometry roots and persistent geometry components.
Mason and CypherTileEditor object IDs identify host scene objects. Host adapters
own the join between those domains. Geometry serialization and Cook never embed
host object types or live handles.

### Document boundary

The Geometry `Document` module is an aggregate geometry store, not the Mason
scene document. It owns representation pools, the source-ID registry, policy,
revision publication, and immutable snapshots. Host documents own entities,
hierarchy, layers, prefabs, global selection, materials, and editor history.

### Numerical boundary

Authored geometry and intermediate construction use binary64. Float conversion
is checked and occurs only at explicit runtime/cook boundaries. Approximate
normalization shortcuts never decide orientation, sidedness, intersection
topology, welding, or canonical ordering.

## Dependency direction

```text
Cypher::CommonTier1 + Cypher::Math
  -> Core identity, results, limits, diagnostics, scratch, operation context
  -> Kernel + low-level Attributes schema/storage
  -> Representations + Intermediates + pure Primitives
  -> Document store + immutable Snapshot
  -> Transactions + ChangeSet/remap/provenance
  -> Queries + Planar
  -> Validation + Tessellation
  -> Selection + Exchange/Sanitation + Spatial + Constraints
  -> Attribute propagation + Euler
  -> leaf Operations + pure Brush/Mesh CSG + Procedural evaluators + Repair plans
  -> composed Modeling/Conversion + Repair application + optional Modifiers
  -> Serialization + Cook
```

Serialization and Cook are sink dependencies, but every new representation adds
its own serialization and cook support in the same vertical slice once those
sink contracts exist.

## Delivery gates

### Gate 0: Core closeout and policy decisions

Deliver:

- the existing identity, generation-pool, diagnostic, scratch, and policy work;
- operation context carrying policy, scratch, diagnostics, and cancellation;
- explicit unit/axis policy and limits for every record family introduced by the
  next gate;
- the geometry-ID versus host-object-ID boundary;
- the primitive-retention decision recorded above.

Close when every stale handle, exhausted budget, allocator failure, corrupt
bounded input, and cancelled operation returns its documented status without
changing registry membership, allocator sequence, scratch cursor, or published
state.

### Gate 1: double-precision Kernel and attribute schema

Deliver in separate snippets:

1. binary64 vector and plane primitives in `Cypher::Math`;
2. checked float/double conversion and finite/range validation;
3. line-plane and three-plane controlled construction;
4. exact-sign or filtered/exact-fallback `orient2d` and `orient3d`;
5. Geometry-owned canonical coordinate keys, total ordering, and policy-aware
   point/plane classification;
6. low-level typed attribute schema/storage, opaque material references, UV
   projection records, and explicit corner/edge/face/side domains.

Close when orientation, sidedness, canonical ordering, and conversion cases are
deterministic across input permutations and documented coordinate limits, or
return `INDETERMINATE`/range failure without publishing output. Attribute
allocation, copy, and validation must be bounded and failure-atomic.

### Gate 2: plane-defined brush and first primitive

Deliver `BrushSolid`, persistent side identity, derived boundary reconstruction,
a pure six-plane box generator, and brush quick/deep validation.

Close when six valid planes produce exactly 8 canonical boundary vertices,
12 edges, and 6 outward faces. Plane-order permutations produce equivalent
canonical traversal. Duplicate, contradictory, unbounded, non-finite, and
degenerate plane sets fail without publishing a brush.

### Gate 3: Geometry document, snapshots, and transactions

Deliver the geometry-local aggregate store, revisions, immutable snapshots,
preview journals, commit/cancel, typed deltas, remaps, provenance, and change
sets.

Close when repeated preview replacement of one canonical brush value creates one
committed revision and one inverse delta; cancel, invalid input, stale revision,
allocation failure, and cancellation restore exact authored state; previously
published snapshots remain unchanged. The first real side-plane drag integration
belongs to Gate 5.

### Gate 4: queries, tessellation, spatial data, selection, and constraints

Deliver brush/root/component queries, 12-triangle box tessellation, bounds
indices followed by derived pick caches, component selection sets, snapping
candidates, and deterministic constraint resolution.

Close when every box triangle maps to one source side and points outward;
indexed query/refit/rebuild results match a retained brute-force reference; and
grid/angle/component snapping is idempotent with documented tie behavior and
never performs an implicit weld.

### Gate 5: first complete brush edits

Deliver transforms, side-plane drag, clip, slice, and bisect with attribute
propagation, selection remapping, and spatial invalidation.

Close when every edit follows preflight -> preview -> validate -> commit or
rollback; world-locked UVs remain fixed in world space; no-ops create no
revision; and undo restores exact authored state, selection mappings, and query
results.

### Gate 6: brush CSG and architectural primitives

Deliver brush intersection and subtraction, then bounded union decomposition
where required. Wedge, prism, stair, and arch creation commands may compose the
same brush construction and clipping paths at this operation layer. Their pure
parameter records may remain under `Primitives`, but low-level primitive
generators do not depend upward on public CSG operations.

Close when disjoint, contained, point/edge/face-touching, shared-plane, rotated,
sliver, and fragment-limit fixtures yield validated regularized output or
bounded diagnostics. Every output side has provenance and failure leaves both
operands unchanged.

### Gate 7: first serialization, Cook, and TileEditor slice

Deliver versioned brush source data, deterministic writer/reader, immutable
render Cook, geometry source mapping, content hash, and the first
CypherTileEditor adapter.

Close when write/read/write bytes are identical; malformed input stays within
budgets; repeated supported configurations yield identical neutral Cook bytes
and hashes; and one selected tile-derived brush can render, pick, preview,
commit, undo, and rebuild without changing `.cymap` version 3 semantics.

### Gate 8: editable-mesh topology foundation

Deliver mesh, shell, vertex, half-edge, edge, loop, and face pools; a checked
builder; immutable views; structural validation; and base attribute domains.

Close when a canonical box has 8 vertices, 12 edges, 24 half-edges, 6 loops,
6 faces, one shell, Euler characteristic 2, outward winding, positive volume,
and reciprocal valid adjacency. Removed/reused slots reject stale handles.

### Gate 9: Euler and atomic topology edits

Deliver move vertex, split edge, split face, collapse, explicit weld, and
dissolve with transaction, selection-remap, attribute, and spatial integration.

Close when every operation states and checks pre/postconditions, exact undo/redo
restores the original, ambiguous selection remaps are reported, and spatial
query results remain equal to brute force.

### Gate 10: planar regions and neutral sanitation

Deliver planar regions with holes, arrangements, overlay, constrained
triangulation, bounded polygon/triangle soup records, `Exchange/Sanitation`, and
cross-document clone/remap.

Close when disconnected and holed coplanar overlay is deterministic with segment
provenance; malformed/non-manifold soup never silently enters EditableMesh; and
cross-document insertion is collision-free and atomic.

### Gate 11: modeling, conversion, and explicit repair

Deliver, in dependency order, extrude/cut/cap, inset/bridge,
chamfer/bevel/solidify, brush-to-mesh conversion, and explicit repair plans for
orientation, degeneracy, T-junctions, and holes.

Close when every operation declares material/UV/new-face policy, fragmentation
policy, selection remap, validation, and inverse behavior; repair preview does
not mutate; and conversion preserves side/material/UV provenance.

### Gate 12: mesh CSG

Deliver the staged pipeline:

```text
normalize -> broad phase -> predicates/intersections -> corefinement
  -> coplanar overlay -> classification/expression -> boundary extraction
  -> reconstruction/stitching -> attribute transfer -> cleanup/validation
```

Close when union/intersection/difference pass disjoint, containment, touching,
coincident, partial-coplanar, nested-shell, multiple-component, sliver, and
operand-order corpora. Every stage honors event, scratch, recursion, and
fragment limits. Failure publishes witnesses without mutation.

### Gate 13: extended source and procedural families

Add CurveNetwork and Patch before sweep/loft/lathe, subdivision after mesh crease
attributes, and HeightField before displacement. Add retained procedural or
modifier source only if an exercised workflow justifies it.

Each family closes as its own vertical slice with bounded parameters,
deterministic evaluation, validation, transactions, selection behavior, source
round-trip, tessellation/Cook mapping, malformed corpus, cancellation, and a
representative benchmark where warranted.

### Gate 14: incremental and peer-facing Cook

Deliver dependency tracking, compiler interchange, render products, and then
neutral collision, navigation, visibility, and lighting inputs as peer-system
contracts become real.

Close when full and incremental Cook produce identical bytes; one bounded source
change invalidates exactly its dependency closure; stale or cancelled jobs
cannot publish; and representative throughput and memory budgets are recorded.

## Test, property, golden, fuzz, and benchmark policy

- Unit and bounded deterministic property tests use Catch2. Fixed seeds and case
  indices are captured so every failure is reproducible.
- The existing Geometry contract target remains one executable while it is fast.
  It splits by ownership only when runtime, linking, or failure isolation makes
  that useful.
- Golden fixtures begin with a versioned canonical serialization or Cook format.
  Tests never rewrite expected fixtures automatically.
- Coverage-guided fuzzing begins at a real byte or operation-stream boundary:
  deserialization, a checked topology builder, edit stream, planar overlay, or
  CSG stage. Every found crash becomes a minimized corpus entry and a normal
  deterministic regression.
- Executable benchmarks live under `benchmarks/CypherEditor/Geometry/`, use the
  existing Google Benchmark presets and runner, and are excluded from ordinary
  builds. Hosted CI compiles them but does not enforce noisy wall-time limits.
- A benchmark records input scale, items/bytes processed, setup exclusion,
  allocator policy, and the algorithmic claim being measured.

## Frontend and peer-system boundary

The following remain outside Geometry:

- Qt widgets, themes, panels, icons, menus, hotkeys, layouts, loading screens,
  settings, and workspace persistence;
- viewport cameras, gestures, manipulators, active tool/workplane, pointer
  capture, and numeric-entry presentation;
- scene hierarchy, entities, triggers, lights, audio, scripted sequences,
  prefabs, layers, and editor-wide history;
- material/resource databases and asset browsers;
- physics decomposition, navmesh policy, PVS generation, lighting, acoustics,
  runtime ownership, autosave, build orchestration, and live IPC.

Those systems consume stable geometry IDs, immutable snapshots, change sets,
source mappings, and neutral Cook inputs. Their existence does not change the
Geometry dependency direction.
