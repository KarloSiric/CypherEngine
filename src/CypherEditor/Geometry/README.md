# Cypher Editor Geometry

`Cypher::EditorGeometry` is the Qt-free authoring-geometry library shared by
CypherTileEditor, the future Mason map workspace, focused geometry tools, and the
authoring side of the map compiler.

The library does **not** force every authored object into one mesh topology.
Brushes, editable meshes, planar polygons, patches, and triangle soup have
different invariants and remain separate source representations. Conversion is
explicit and records source provenance. Immutable cooked products are derived
from those representations for rendering, collision, navigation, visibility,
lighting, and compiler interchange.

## Dependency rules

Allowed public dependencies:

- `Cypher::CommonTier1`
- `Cypher::Math`

Forbidden dependencies:

- Qt and editor-widget types;
- CypherTileEditor or Mason document types;
- camera/input/tool/gizmo state;
- renderer backend objects;
- mutable `CypherWorld` state;
- physics-engine objects;
- platform/window APIs.

## Shared contracts

- Persistent authoring identity uses document-stable source IDs.
- Live storage uses typed generation-checked handles, never persistent pointers.
- Topology and representation conversion publish explicit source remaps.
- Mutations are transactional; failure leaves the source state unchanged.
- Validation emits bounded diagnostics and never silently repairs geometry.
- Brush planes remain canonical for brush editing; reconstructed polygons are
  caches, not a replacement source of truth.
- Editable meshes own explicit adjacency; temporary/import triangle soup does
  not pretend to satisfy mesh manifold invariants.
- Runtime triangle/index buffers are disposable cooked products.

## Module ownership

```text
Core/             identity, handles, results, budgets, allocation contracts
Kernel/           scalar policy, quantization, predicates, constructions, ordering
Representations/  Brush, Mesh, Polygon2D, Patch, and TriangleSoup source models
Attributes/       schemas, typed layers, UV/material/normal data and propagation
Planar/           arrangements, holes, overlay, offset, constrained triangulation
Queries/          ray casts, adjacency, containment, measurements, feature queries
Validation/       structural, geometric, representation, and solid diagnostics
Repair/           explicit previewable and undoable repair plans
Transactions/     preview journals, invertible deltas, remapping, provenance
Selection/        geometry-component sets and topology-aware selection queries
Exchange/         fragments, import sanitation, clone/extract/insert boundaries
Primitives/       deterministic parametric brush, mesh, polygon, and patch sources
Operations/       transform, cutting, topology, modeling, conversion, Euler edits
Modifiers/        non-destructive mirror, arrays, bend, taper, sweep, rebuild
Csg/              separate brush and mesh Boolean paths plus reconstruction stages
Spatial/          editable indexes, caches, picking candidates, dirty regions
Tessellation/     deterministic representation-to-triangle tessellation
Procedural/       curve, sweep, patch, subdivision, and displacement generators
Serialization/    versioned authored geometry, stable IDs, deterministic migration
Cook/             multiple immutable render/compiler/gameplay target products
```

Every module directory contains an ownership contract and planned implementation
units. Source files are added only with an implemented contract and focused tests;
the scaffold deliberately contains no empty C++ placeholders.

Read [ARCHITECTURE.md](ARCHITECTURE.md) for the representation contracts,
dependency layers, operation and CSG pipelines, implementation gates, test plan,
and the strict TileEditor/Mason boundary. The shared-library decision is recorded
in [ADR 0005](../../../docs/adr/0005-shared-editor-geometry-core.md).

## First implementation path

The first useful vertical path is brush-first: define the kernel policy, create a
plane-defined convex box, reconstruct and validate its boundary, clip and drag one
face transactionally, preserve texture projection, tessellate deterministically,
cook a render preview with source mapping, and expose it through a TileEditor
adapter. General editable-mesh Boolean CSG begins only after those contracts pass.
