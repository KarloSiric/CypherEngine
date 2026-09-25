# Cook

Immutable, revisioned products derived from validated snapshots: tessellation, render meshes, collision, navigation, visibility, lightmap charts, batching, source mapping, dependency keys and the compiler interchange.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_BrushTessellation` | Fan-triangulated output from a brush boundary |
| `CypherGeometry_CompilerInterchange` | The compiler interchange: a versioned, bounded, self-checking CYKV snapshot of the cooked map surfaces plus their dependency keys, for headless compilers (BSP/vis, lightmapper,... |
| `CypherGeometry_CookBatching` | Static batch proposals: the map's surface triangles grouped by material and by spatial chunk, for a renderer or world builder to turn into draw batches and streaming cells |
| `CypherGeometry_CookBytes` | The little-endian byte writer used to build hashable canonical encodings for cook keys and cook product hashes |
| `CypherGeometry_CookCollision` | The collision cook: immutable snapshot in, bounded static collision surfaces out (binary32 positions, triangles, per-triangle source element IDs), with diagnostics tied to source... |
| `CypherGeometry_CookKeys` | Deterministic cook dependency keys: a content hash per source object in an immutable snapshot, a policy hash, product keys derived from them, and the diff between two key sets... |
| `CypherGeometry_CookLighting` | Lightmap charts: the map's surfaces cut into charts that each flatten without overlap, laid out in atlas pages, and the resulting lightmap UV for every triangle corner - what a... |
| `CypherGeometry_CookNavigation` | The navigation cook input: the surface triangles a navmesh generator should consider walkable, by slope alone, each traced to its source object and element |
| `CypherGeometry_CookSourceMapping` | Cooked geometry products with source provenance |
| `CypherGeometry_CookSurfaces` | The shared cook input: every surface of a snapshot (brushes, meshes, patches, heightfields) as one double-precision triangle soup, each triangle carrying its source object,... |
| `CypherGeometry_CookVisibility` | The visibility cook input: the geometry a visibility compiler (BSP + portals + PVS, or an occlusion baker) may treat as sealing, separated from the geometry it must not |
| `CypherGeometry_Cook_RenderMesh` | The render-mesh cook product for EditableMesh: seam- expanded float vertices, indices, tangents, material batches, bounds, triangle -> face source map, and a content hash |
| `CypherGeometry_HeightFieldTessellation` | Per-tile HeightField tessellation with level of detail, crack-free stitching to coarser neighbours, and source mapping from every vertex and triangle back to authored data |
| `CypherGeometry_MeshTessellation` | Deterministic tessellation of an EditableMesh into indexed triangles with complete triangle -> face source mapping |
| `CypherGeometry_PatchTessellation` | Deterministic, crack-free triangulation of a Patch with per-vertex and per-triangle provenance |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Cook

#### Owns

Immutable, revisioned, disposable products derived from validated source
geometry, with complete source mapping, content hashes, and deterministic
dependency keys for bounded incremental invalidation.

#### Does not own

Runtime subsystem policy, mutable authored data, or GPU/backend ownership.

#### First acceptance gate

Cook the same source and policy to identical outputs and hashes across repeated supported-platform runs.

### Tessellation

#### Owns

Deterministic conversion of brushes, polygon faces with holes, patches, and approved soup into indexed triangles with provenance.

#### Does not own

Render batching, GPU upload, or authored source replacement.

#### First acceptance gate

Tessellate the brush box to exactly 12 outward triangles and map every triangle to one source side.

### Cook / DependencyGraph

#### Owns

Deterministic dependencies from source revisions and policy hashes to immutable
cook products, plus dirty propagation and reusable product keys.

#### Does not own

Background-job scheduling, file watching, build orchestration, autosave, or live
editor IPC.

#### First acceptance gate

Change one bounded brush attribute, invalidate exactly the affected products,
and retain byte-identical keys for every unaffected product.

### Cook / SourceMapping

#### Owns

Cooked triangle and product records back to geometry root/component source IDs
and source representation kinds.

#### Does not own

Mason/CypherTileEditor object IDs, live handles, human-readable diagnostic
presentation, or the host adapter that joins a geometry root to a scene object.

#### First acceptance gate

Resolve every cooked primitive in canonical fixtures back to its authored origin.

### Cook / RenderMesh

#### Owns

Seam-expanded render vertices, indices, normals, tangents, material batches, bounds, and source-triangle maps.

#### Does not own

GPU buffers or draw submission.

#### First acceptance gate

Produce the canonical box render mesh and stable batches.

### Cook / Collision

#### Owns

Neutral geometric inputs and source mapping for the physics cooker.

#### Does not own

Physics shape policy, decomposition, simulation, or runtime ownership.

#### First acceptance gate

Publish bounded static surface/convex inputs with diagnostics tied to source elements.

### Cook / Navigation

#### Owns

Walkable surface candidates and source mapping for a navigation cooker.

#### Does not own

Agent rules, navmesh generation, off-mesh links, or path queries.

#### First acceptance gate

Export geometry candidates without claiming gameplay walkability.

### Cook / Visibility

#### Owns

Neutral closed-surface and portal geometry inputs for later visibility compilers.

#### Does not own

PVS policy or runtime culling.

#### First acceptance gate

Publish validated source-mapped regions only when the map contract requires them.

### Cook / Lighting

#### Owns

Lightmap chart surfaces and source mapping required by a future lighting compiler.

#### Does not own

Lighting equations, baking, probes, or renderer light resources.

#### First acceptance gate

Publish stable surface charts only after material/renderer contracts require them.

### Cook / Batching

#### Owns

Deterministic static grouping and spatial chunk proposals derived from geometry/material identity.

#### Does not own

World streaming policy or runtime object lifetime.

#### First acceptance gate

Produce stable batches without merging across provenance or material boundaries.

### Cook / CompilerInterchange

#### Owns

Versioned neutral snapshots consumed by headless compilers and peer systems.

#### Does not own

Final packaged runtime map formats.

#### First acceptance gate

Serialize a bounded immutable snapshot with revision, policy, hashes, and dependency metadata.
