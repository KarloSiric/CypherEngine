# Cypher Editor Geometry

`Cypher::EditorGeometry` is the Qt-free authoring-geometry library shared by
CypherTileEditor, the future Mason map workspace, focused geometry tools, and the
authoring side of the map compiler.

The library does **not** force every authored object into one mesh topology.
Brushes, editable meshes, planar regions, patches, retained curve networks, and
height fields have different invariants and remain separate source
representations. Triangle soup is neutral import and operation storage rather
than an ordinary authored object. Conversion is explicit and records source
provenance. Immutable cooked products are derived from validated snapshots for
rendering, collision, navigation, visibility, lighting, and compiler interchange.

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
- Live storage uses representation-qualified generation handles, never
  persistent pointers. Compact handles remain local to the document pool that
  issued them; cross-document exchange uses source IDs and explicit remapping.
- Topology and representation conversion publish explicit source remaps.
- Mutations are transactional; failure leaves the source state unchanged.
- Validation emits bounded diagnostics and never silently repairs geometry.
- Brush planes remain canonical for brush editing; reconstructed polygons are
  caches, not a replacement source of truth.
- Editable meshes own explicit adjacency; temporary/import triangle soup does
  not pretend to satisfy mesh manifold invariants.
- Runtime triangle/index buffers are disposable cooked products.

## Folder index

The library lives in ten shallow folders. Headers are included by basename
(`#include "CypherGeometry_BrushSolid.h"`), so folders exist only to make the
code findable. ARCHITECTURE.md still names the finer-grained logical modules
(Kernel, Transactions, Tessellation, ...) as dependency layers; the right-hand
column shows which folder each one lives in. Every folder README lists its
headers with a one-line purpose and carries the merged ownership contracts.

| Folder | Holds | Logical modules |
| --- | --- | --- |
| [Core/](Core/README.md) | identity, handles, policy, diagnostics, scratch, exact predicates, planar toolkit, attribute schemas, spatial index | Core, Kernel, Planar, Spatial, Attributes/Schema |
| [Brush/](Brush/README.md) | the plane-defined convex brush and every brush edit | Representations/Brush, brush construction, transform, clip, vertex ops, brush CSG |
| [Mesh/](Mesh/README.md) | the editable mesh and its modelling operations | Representations/Mesh, Modeling, Topology, Euler, Subdivision, Attributes/Propagation |
| [Surfaces/](Surfaces/README.md) | patches, height fields, curve networks, planar regions and their generators | Representations/{Patch, HeightField, CurveNetwork, PlanarRegion}, Procedural |
| [Operations/](Operations/README.md) | cross-representation tools | Primitives, Modifiers, Constraints, Conversion, UV alignment, Painting |
| [Csg/](Csg/README.md) | staged mesh Booleans, the brush path, document commands | Csg |
| [Document/](Document/README.md) | the authoring store and how it changes and persists | Document, Transactions, Serialization, Exchange |
| [Cook/](Cook/README.md) | immutable products derived from snapshots | Cook, Tessellation |
| [Validation/](Validation/README.md) | checking and fixing untrusted geometry | Validation, Repair, Intermediates, Sanitation |
| [Queries/](Queries/README.md) | read-only queries and selection | Queries, Selection |

Tests mirror the same folders under `tests/CypherEditor/Geometry/`.

Read [ARCHITECTURE.md](ARCHITECTURE.md) for the representation contracts,
dependency layers, operation and CSG pipelines, and the strict TileEditor/Mason
boundary. [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) defines the measurable
delivery gates, snippet workflow, and test/benchmark policy. The shared-library
decision is recorded in
[ADR 0005](../../../docs/adr/0005-shared-editor-geometry-core.md).

## First implementation path

The first useful vertical path is brush-first: define the kernel policy, create a
plane-defined convex box, reconstruct and validate its boundary, clip and drag one
face transactionally, preserve texture projection, tessellate deterministically,
cook a render preview with source mapping, and expose it through a TileEditor
adapter. General editable-mesh Boolean CSG begins only after those contracts pass.
