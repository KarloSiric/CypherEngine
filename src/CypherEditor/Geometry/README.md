# Cypher Editor Geometry

`Cypher::EditorGeometry` is the Qt-free authoring geometry library shared by
CypherTileEditor, the future Mason map workspace, focused geometry tools, and the
map compiler's authoring-side validation path.

It is not a renderer, physics engine, runtime world representation, UI toolkit,
or source-file parser. It owns editable topology and geometry mutations. Consumers
adapt its immutable preview/cook products to their own renderer, document, or
compiler contracts.

## Dependency rules

Allowed public dependencies:

- `Cypher::CommonTier1`
- `Cypher::Math`

Forbidden dependencies:

- Qt
- CypherTileEditor GUI types
- Mason workspace types
- renderer backend objects
- `CypherWorld` mutable state
- physics-engine objects
- platform/window APIs

## Storage rules

- Persistent authoring identity uses document-stable source IDs.
- Live adjacency uses generation-checked handles, never persistent pointers.
- Mutations are transactional and publish topology remaps on commit.
- Invalid operations leave the source state unchanged.
- Validation reports bounded diagnostics; it does not silently repair geometry.
- Runtime triangle/index buffers are derived products and never become the source
  topology.

## Planned module ownership

```text
Core/          IDs, handles, status, numerical policy, scratch contracts
Topology/      mesh, vertex, half-edge, edge, loop, face, shell storage
Validation/    invariants, diagnostics, sanitation and explicit repair plans
Transactions/  mutation journal, remapping, undo/redo payloads
Primitives/    boxes, wedges, prisms, cylinders, arches, stairs
Operations/    split, collapse, weld, extrude, inset, knife, bridge, bevel
Csg/           intersection arrangement, classification, Boolean reconstruction
Attributes/    UVs, normals, tangents, materials, smoothing and crease data
Spatial/       editable BVH, component picking, snapping candidate queries
Cook/          deterministic triangulation and immutable compiler snapshots
```

Only `Core/` is introduced in the initial scaffold. Each additional directory is
added with its first implemented vertical slice and focused tests. The complete
roadmap and the TileEditor/Mason ownership decision are recorded in
`docs/adr/0005-shared-editor-geometry-core.md`.

## First implementation gate

The first real topology slice must create one box, validate it as a closed
orientable manifold, traverse all face loops deterministically, triangulate it,
undo one mutation, and expose stable IDs to a TileEditor adapter. General CSG does
not begin before this gate passes.

