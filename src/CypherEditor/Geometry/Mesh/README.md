# Mesh

The editable polygon mesh (half-edge source, builder, validation) and its modelling operations: bevel, bridge, knife, slice, merge, loop slide, solidify, topology edits, Euler operators, subdivision surfaces and attribute propagation through edits.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_EditableMesh` | The editable half-edge mesh and its element records |
| `CypherGeometry_Euler` | The Euler operators: the minimal split/join topology edits of the editable mesh, each with checked preconditions, a guaranteed element-count change, and an exact inverse |
| `CypherGeometry_MeshAttributeTransfer` | The capture/resolve engine that carries face, corner, and edge attributes (and optionally face identity) across a topology edit on a mesh source |
| `CypherGeometry_MeshBevel` | The multi-edge, multi-segment edge bevel (chamfer for one segment, rounded profile for more) on the editable mesh |
| `CypherGeometry_MeshBooleans` | Mesh-level boolean operations on editable meshes |
| `CypherGeometry_MeshBoundaryOps` | Open-boundary topology operations on the editable half-edge mesh |
| `CypherGeometry_MeshBridge` | Hammer's Bridge on the editable mesh for the two cases MeshBoundary_Bridge (whole open loops) does not cover |
| `CypherGeometry_MeshBuilder` | The checked mesh builder for constructing half-edge meshes from validated brush boundaries |
| `CypherGeometry_MeshCleanup` | Low-level editable-mesh cleanup primitives, deep clone, and per-corner auto-smooth normal evaluation |
| `CypherGeometry_MeshCornerNormals` | Attribute-driven split normals for an EditableMesh: one normal per corner, smooth across edges that are neither HARD nor separating disjoint smoothing groups |
| `CypherGeometry_MeshEditBracket` | The capture -> op -> resolve bracket shared by every identity-addressed mesh edit |
| `CypherGeometry_MeshKnife` | The knife cut on the editable half-edge mesh: a path of points on vertices, on edges, and inside faces that splits every face it crosses |
| `CypherGeometry_MeshLoopSlide` | A source-identity addressed, baseline-relative slide of one regular closed edge loop |
| `CypherGeometry_MeshLoopTraversal` | A dynamically sized traversal for a regular closed edge loop in an editable quad mesh |
| `CypherGeometry_MeshMerge` | Vertex merging on the editable mesh, and the two Hammer workflows built on it: sewing open edges together, and merging vertices that lie within a distance of each other |
| `CypherGeometry_MeshObjectCommands` | Document commands for exact mesh Join and Separate |
| `CypherGeometry_MeshPlanar` | Internal exact 2D polygon checks shared by the mesh operations that reshape faces (knife, bevel): axis-drop projection, exact simplicity, and exact orientation |
| `CypherGeometry_MeshQuadSlice` | Hammer's Quad Slice on the editable mesh: cutting selected quads into a grid of cU x cV quads |
| `CypherGeometry_MeshRecordAccess` | Internal record-access and capacity helpers shared by the editable-mesh operation translation units (boundary ops, knife, and the operations that follow them) |
| `CypherGeometry_MeshSlice` | Cutting an editable mesh with a plane - Hammer's Clipping tool on meshes |
| `CypherGeometry_MeshSolidify` | Bounded solidification of open polygonal mesh sources |
| `CypherGeometry_MeshSource` | The document-level Mesh source: an editable half-edge mesh plus its authored attributes and the persistent source identity of its elements, together with a flat canonical... |
| `CypherGeometry_MeshSourceBevel` | Identity-addressed wrapper for the multi-edge, multi-segment bevel (MeshBevel_Edges) on a mesh source |
| `CypherGeometry_MeshSourceBridge` | Identity-addressed wrappers for bridging two faces or two open edge chains (MeshBridge.h). |
| `CypherGeometry_MeshSourceComponents` | Component transforms on a mesh source: move / rotate / scale the selected vertices, edges, or faces, offset them along their normals, and flatten them onto a plane |
| `CypherGeometry_MeshSourceComposition` | Exact composition operations for authored mesh sources |
| `CypherGeometry_MeshSourceMerge` | Identity-addressed wrappers for vertex merging (MeshMerge.h): merge groups, sew open edges, merge by distance |
| `CypherGeometry_MeshSourceModeling` | Identity-addressed modeling edits on a mesh source: extrude, inset, bevel, loop cut, triangulation, transforms, and direct attribute edits |
| `CypherGeometry_MeshSourceSlice` | Identity-addressed wrapper for cutting a mesh source with a plane (MeshSlice_ByPlane): slice, clip, and capped clip |
| `CypherGeometry_MeshSourceTopology` | Identity-addressed topology edits on a mesh source |
| `CypherGeometry_MeshSubdivision` | Mesh subdivision surface operations |
| `CypherGeometry_MeshToBrush` | The mesh-to-brush conversion that closes the editing round-trip: brush → boundary → mesh → edit → brush |
| `CypherGeometry_MeshTopologyOps` | Topology-editing operations on the editable mesh |
| `CypherGeometry_MeshValidation` | Structural validation for editable half-edge meshes |
| `CypherGeometry_MeshVertexMove` | The validated multi-vertex move: the mesh-level core of component transforms (move / rotate / scale / offset a selection) |
| `CypherGeometry_SubdivisionSurface` | Retained subdivision: a descriptor evaluated from an authored mesh without changing it, with creases, open-mesh boundaries, interpolated surface data, and source mapping |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Representations

#### Owns

Canonical BrushSolid, EditableMesh, PlanarRegion, PatchSurface, CurveNetwork,
and HeightField source models, plus rules for explicit conversion with
provenance. Neutral soup storage belongs to `Intermediates`.

#### Does not own

A universal topology. Brush planes, manifold meshes, planar regions, parametric
surfaces, retained curves, height samples, and raw soup retain different
invariants.

#### First acceptance gate

Expose immutable representation views and checked conversion entry points without losing source IDs.

### Representations / Mesh

#### Owns

Editable oriented polygonal two-manifold storage: mesh, shell, vertex, half-edge, edge, loop, and face pools plus deterministic traversal.

#### Does not own

Malformed imports, convex brush plane sets, renderer vertex buffers, or non-manifold Boolean intermediates.

#### First acceptance gate

Build the canonical closed box with 8 vertices, 12 edges, 24 half-edges, 6 loops, 6 faces, one shell, and Euler characteristic 2.

### Operations / Modeling

#### Owns

Transactional extrude, inset, bevel, chamfer, knife, loop cut, subdivide,
solidify, and orchestration that bakes/collapses sweep, lathe, loft, or modifier
evaluation into canonical source representations.

#### Does not own

General Boolean evaluation, the pure Procedural evaluator, or permanent
procedural parameter ownership.

#### First acceptance gate

Extrude a face with attribute propagation, source remap, validation, deterministic output, and exact undo.

### Operations / Topology

#### Owns

Split, collapse, weld, dissolve, merge, separate, bridge, fill, stitch, and orientation flips.

#### Does not own

Non-destructive modifier stacks or interaction sessions.

#### First acceptance gate

Complete atomic edge split, face split, collapse, weld, and dissolve in dependency order.

### Operations / Euler

#### Owns

Minimal make/kill, split/join, and edge/face topology primitives with formal preconditions and postconditions.

#### Does not own

High-level user gestures or implicit repair.

#### First acceptance gate

Implement edge split and face split with exhaustive invariant and stale-handle tests.

### Procedural / Subdivision

#### Owns

Bounded subdivision descriptors, crease rules, evaluation, and source mapping.

#### Does not own

Destructive base topology mutation unless explicitly collapsed.

#### First acceptance gate

Evaluate one level reproducibly with crease propagation.

### Attributes / Propagation

#### Owns

Operation-specific interpolation, generated-face policy, world-locked versus
geometry-locked texture behavior, smoothing/crease transfer, conflict handling,
and provenance for topology-changing edits, conversion, repair, and CSG.

#### Does not own

Base attribute storage, topology mutation, transaction publication, material
asset loading, or presentation.

#### First acceptance gate

Move one brush side through a preview transaction while preserving its material,
provenance, and world-locked texture projection; cancel and undo restore the
exact original records.
