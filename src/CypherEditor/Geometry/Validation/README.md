# Validation

Checking and fixing untrusted geometry: geometric validation, explicit repair plans, and the bounded neutral soups (polygon and triangle) with the sanitation path from imported data to checked representations.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_MeshGeometricValidation` | Geometric (as opposed to structural) validation of an editable mesh: short edges, small faces, warped faces, coincident vertices, and self-intersection |
| `CypherGeometry_PointWeld` | Explicit, deterministic point welding: a remap from input points to cluster representatives |
| `CypherGeometry_PolygonSoup` | PolygonSoup, the bounded neutral record for indexed polygons that carry no authoritative adjacency |
| `CypherGeometry_Repair` | Explicit, previewable repair of polygon soups and editable meshes |
| `CypherGeometry_Sanitation` | The checked conversion from PolygonSoup to EditableMesh, and the reverse export |
| `CypherGeometry_TriangleSoup` | TriangleSoup: bounded independent triangles as they arrive from importers (STL-style: three positions per triangle, no shared vertices), and the explicit weld into PolygonSoup |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Validation

#### Owns

Bounded structural, geometric, attribute, brush, manifold, and closed-solid diagnostics with quick and deep modes.

#### Does not own

Mutation or automatic healing. Validation may propose repair IDs but never applies them.

#### First acceptance gate

Detect dangling relationships, degeneracy, non-planarity, self-intersection, open shells, invalid brush planes, duplicate IDs, and non-finite coordinates.

### Repair

#### Owns

Explicit previewable plans for orientation, duplicate removal, degenerate removal, boundary stitching, hole filling, T-junction resolution, and non-manifold separation.

#### Does not own

Silent epsilon welding or mutation initiated by validation.

#### First acceptance gate

Preview one bounded repair plan, apply it transactionally, and restore the exact original state through undo.

### Intermediates

#### Owns

Bounded neutral records used between import, validation, repair, planar,
tessellation, and CSG stages. These records may temporarily lack authored
adjacency or contain geometry that has not passed representation invariants.

#### Does not own

Persistent authored objects, semantic scene volumes, or cooked runtime assets.
Intersection graphs, corefined faces, classified cells, and boundary fragments
remain private to the operation pipeline that defines their invariants.

#### First acceptance gate

Accept bounded triangle and polygon soup, preserve input provenance, emit stable
diagnostics for malformed data, and publish a source representation only after
an explicit checked conversion succeeds.

### Intermediates / PolygonSoup

#### Owns

Bounded polygon faces, corner streams, source provenance, and optional grouping
without promising authoritative manifold adjacency.

#### Does not own

EditableMesh invariants, automatic welding, semantic object identity, or silent
publication as authored geometry.

#### First acceptance gate

Round-trip bounded disconnected polygons, diagnose invalid rings and non-finite
coordinates, and convert only validated connected components through an explicit
result and remap.

### Intermediates / TriangleSoup

#### Owns

Neutral indexed or independent triangles for import, sanitation, CSG intermediates, and diagnostics on data that does not satisfy mesh invariants.

#### Does not own

Authoritative manifold adjacency or silent conversion into EditableMesh.

#### First acceptance gate

Accept bounded malformed input, diagnose it, and convert only sanitized components into checked representations.

### Exchange / Sanitation

#### Owns

Bounded orchestration from neutral PolygonSoup, TriangleSoup, and versioned
exchange records into checked canonical source representations. It requests
Validation diagnostics, applies explicit caller-selected sanitation policy, and
publishes complete source/remap/provenance records through checked builders.

#### Does not own

File-format parsing, silent geometric repair, host object identity, material
asset loading, or relaxed invariants in canonical Brush or EditableMesh storage.
Intermediates own raw records; Validation reports faults; Repair owns explicit
undoable source changes after import.

#### First acceptance gate

Accept a bounded valid polygon soup into a checked representation and reject
non-finite, duplicate, non-manifold, over-budget, and ambiguous input without
partially publishing source elements.
