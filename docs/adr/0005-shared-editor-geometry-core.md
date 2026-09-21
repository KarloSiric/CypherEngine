# ADR 0005: Shared Editor Geometry Core

**Status:** Accepted  
**Date:** 2026-09-18

## Context

CypherTileEditor already owns a useful grid-backed `.cymap` document, selection,
history, validation, serialization, materials, and derived floor, wall, door, and
stair boxes. It does not own arbitrary vertices, edges, face loops, polygon meshes,
or Boolean CSG.

Mason will eventually need those facilities, but implementing them inside Qt
widgets or copying a TileEditor implementation into Mason would create divergent
geometry semantics. Editable topology also must not enter `CypherWorld`: runtime
worlds consume compiled render geometry, collision, navigation, visibility,
lighting, and entity payloads.

The repository already provides representation-independent operations through
`Cypher::Math`, including vectors, transforms, rays, planes, bounds, polygon
triangulation, convex-brush reconstruction, clipping, snapping, viewport
projection, UV projection, and gizmo hit testing. The missing layer is an
authoring-owned topology and mutation system above that math foundation.

## Decision

Create one Qt-free shared library:

```text
CypherEditorGeometry
Cypher::EditorGeometry
```

Its source lives under `src/CypherEditor/Geometry`. CypherTileEditor is the first
integration host. Mason will later link the same target; its source will never be
copied into Mason.

The dependency direction is:

```text
Cypher::CommonTier1 + Cypher::Math
                 |
                 v
       Cypher::EditorGeometry
                 |
                 v
   Tile-map topology adapter
                 |
                 v
       CypherTileEditor GUI

Mason -----------------------> Cypher::EditorGeometry
CypherSceneCompiler ---------> validated authoring snapshot
CypherWorld -----------------> cooked output only
```

The initial TileEditor integration exercises shared geometry through transient
adapters rather than replacing the current dense tile grid. An explicit Mason
conversion creates a `.cyscene`; arbitrary mesh edits then belong to that scene
and do not silently round-trip into the original tile primitive.

The geometry core uses:

- document-stable source IDs for selection, diagnostics, serialization, and undo;
- generation-checked live handles for topology access;
- index/handle relationships rather than persistent raw pointers;
- explicit allocator ownership and bounded diagnostics;
- transactional mutations that either commit valid topology or restore the exact
  previous state;
- explicit repair commands instead of silent healing;
- deterministic traversal and triangulation rules;
- separate per-vertex, per-corner, per-edge, and per-face attribute streams;
- separate canonical authoring representations for plane-defined convex brushes,
  editable manifold meshes, planar regions, patches, retained curve networks,
  and height fields;
- neutral triangle/polygon soup and stage-specific CSG records that never pretend
  to be ordinary authored objects;
- explicit, provenance-preserving conversion between representations;
- separate brush and mesh CSG paths, with brush CSG delivered first;
- CSG as an authoring operation over validated solids, not as the runtime world
  representation.

The editor geometry layer is split conceptually into:

1. Core identity, results, limits, allocators, and scratch contracts.
2. Numerical kernel policy, quantization, robust predicates, controlled
   constructions, and deterministic ordering.
3. Brush, mesh, planar-region, patch, curve-network, and height-field source
   representations plus neutral soup processing storage.
4. Attribute schemas, storage, interpolation, texture locking, and propagation.
5. Queries, planar arrangements, spatial indexes, and tessellation.
6. Validation and explicit repair diagnostics.
7. Transactions, remapping, geometry selection, and exchange fragments.
8. Primitive generation, local operations, conversions, and modifiers.
9. Separate brush and mesh Boolean arrangement, classification, and
   reconstruction pipelines.
10. Versioned authoring serialization and compiler-facing immutable cook
    products with complete source mapping.

Curve networks and height fields retain canonical authoring state in their own
representations. Curve evaluation, subdivision surfaces, sweeps, patches, and
displacement remain isolated algorithm families that convert or update source
representations explicitly. Terrain-domain layers, foliage, streaming, collision
decomposition, navigation, lighting, visibility, acoustics, prefabs, entities,
and live-sync are consumers or peer systems. They do not become unrelated methods
on the mesh container or dependencies of this library.

## First vertical slice

The first implementation path is deliberately small and brush-first:

```text
create one six-plane convex brush box
  -> reconstruct and validate its closed boundary
  -> traverse brush sides deterministically
  -> tessellate to twelve source-mapped triangles
  -> drag one side plane through a preview/commit transaction
  -> preserve world-locked texture projection
  -> generate from one TileEditor cell or room piece
  -> render and pick stable brush elements
```

Editable manifold mesh storage follows as a separate representation. Its first
fixture has 8 vertices, 12 edges, 24 half-edges, 6 loops, 6 faces, one shell,
Euler characteristic 2, outward orientation, and positive volume. Only after
those paths work do operations arrive in dependency order:

1. brush plane clipping, face dragging, and brush intersection/subtraction;
2. mesh vertex movement;
3. edge and face split;
4. edge collapse and explicit weld;
5. face extrusion, plane knife/cap, inset, and loop bridge;
6. chamfer, bevel, and solidify;
7. planar arrangements, coplanar overlay, and closed-volume classification;
8. general mesh Boolean union, difference, and intersection;
9. subdivision, sweeps, patches, and displacement.

## Consequences

- TileEditor can test the future Mason geometry workflow immediately without
  becoming Mason or replacing its tile workflow.
- Brush editing keeps planes and per-side texture projection canonical instead
  of abusing manifold mesh topology as universal storage.
- Mason and focused tools receive identical topology, validation, and command
  behavior.
- `CypherSceneCompiler` can consume validated snapshots without Qt dependencies.
- `CypherWorld` remains free of editable topology and undo state.
- Editor sources enter products only through explicit editor/tool targets and
  never become source files of the `CypherEngine` executable.
- `.cymap` version 3 keeps its current tile-grid meaning. Arbitrary Mason geometry
  is persisted by `.cyscene`, as fixed by ADR 0007.
- File count and line count are planning observations, never completion criteria.
  New files are created when a responsibility has an implemented contract and
  focused tests, rather than as an empty thousand-file tree.

## Rejected alternatives

### Implement geometry inside CypherTileEditor and copy it later

Rejected because fixes, serialization, tolerances, and topology behavior would
diverge between products.

### Replace the existing tile document immediately

Rejected because it would discard a working blockout workflow and force an
unnecessary schema migration before the topology core is proven.

### Put editable topology in CypherMath or CypherWorld

Rejected because CypherMath owns representation-independent numerical operations,
while CypherWorld owns cooked runtime data. Selection, commands, undo, and source
topology are editor concerns.

### Treat raw pointers as persistent topology identity

Rejected because raw pointers do not survive reallocation, serialization,
clipboard transfer, undo snapshots, compaction, or stale-reference checks.

### Auto-heal every invalid mutation

Rejected because implicit welding or capping can change authored intent. Normal
operations preserve invariants or roll back; repair and sanitation are explicit,
undoable workflows.
