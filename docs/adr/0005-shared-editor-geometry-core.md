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
CypherMapCompiler -----------> validated authoring snapshot
CypherWorld -----------------> cooked output only
```

The initial TileEditor integration adds a second authored geometry layer rather
than replacing the current dense tile grid. Tile primitives may generate editable
meshes or be converted explicitly. Once converted, arbitrary mesh edits are the
source of truth and do not silently round-trip into the original tile primitive.

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
- a mesh-first persistent authoring representation;
- CSG as an authoring operation over validated closed volumes, not as the runtime
  world representation.

The editor geometry layer is split conceptually into:

1. Core identity, results, numerical policy, allocators, and scratch contracts.
2. Mesh topology: vertices, half-edges, edges, loops, faces, shells, and meshes.
3. Validation and explicit repair diagnostics.
4. Topology transactions, remapping, selection provenance, and undo deltas.
5. Primitive generation.
6. Local mesh operations such as split, collapse, weld, extrude, inset, knife,
   bridge, chamfer, and bevel.
7. Robust Boolean arrangement, classification, and reconstruction.
8. Attribute propagation, UVs, normals, tangents, materials, and creases.
9. Spatial indexing, component picking, and snapping candidates.
10. Deterministic triangulation and compiler-facing immutable snapshots.

Curves, subdivision surfaces, sweeps, displacement sculpting, terrain, collision
decomposition, navigation, lighting, visibility, acoustics, prefabs, entities,
and live-sync are consumers or later peer systems. They do not become unrelated
methods on the base half-edge container.

## First vertical slice

The first implementation gate is deliberately small and complete:

```text
create one box
  -> validate a closed orientable manifold
  -> traverse deterministic face loops
  -> triangulate through Cypher::Math
  -> apply and undo one topology transaction
  -> generate from one TileEditor cell or room piece
  -> render and pick stable topology elements
```

Only after that path works do operations arrive in dependency order:

1. vertex movement;
2. edge split;
3. edge collapse and explicit weld;
4. face split;
5. face extrusion;
6. plane knife and optional cap;
7. inset;
8. loop bridge;
9. chamfer and bevel;
10. closed-volume classification;
11. Boolean union, difference, and intersection;
12. subdivision, sweeps, displacement, and sculpting.

## Consequences

- TileEditor can test the future Mason geometry workflow immediately without
  becoming Mason or replacing its tile workflow.
- Mason and focused tools receive identical topology, validation, and command
  behavior.
- `CypherMapCompiler` can consume validated snapshots without Qt dependencies.
- `CypherWorld` remains free of editable topology and undo state.
- The top-level runtime source glob must exclude `src/CypherEditor` so editor code
  enters products only through explicit targets.
- `.cymap` requires a future schema version before arbitrary geometry objects are
  persisted; version 3 keeps its current meaning.
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

