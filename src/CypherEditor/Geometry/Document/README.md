# Document

The authoring store and everything that changes or persists it: document pools and snapshots, transactions and undoable deltas, serialization, and fragments for copy / paste / duplicate across documents.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_BrushSerialization` | Deterministic CYKV persistence for brush geometry |
| `CypherGeometry_Delta` | Typed geometry change records with inverse computation |
| `CypherGeometry_Document` | The geometry-local aggregate authoring store |
| `CypherGeometry_DocumentBrushAttributes` | The brush surface records the geometry document keeps next to its brushes (material + UV projection per side), and publishing whole authored brushes (brush_source_t) into it |
| `CypherGeometry_DocumentBrushReplacement` | Atomic exact-identity replacement of document brushes |
| `CypherGeometry_DocumentIdentity` | Internal helper that moves a document's identity registry from one object's IDs to another's in one failure-atomic step, for the document stores that add, remove, and replace... |
| `CypherGeometry_DocumentMeshSet` | Atomic N-to-M publication of ordered mesh sets |
| `CypherGeometry_DocumentMeshes` | Document ownership of editable mesh sources: lookup, add, remove, and whole-mesh replacement with exact source-ID registry bookkeeping |
| `CypherGeometry_DocumentRollback` | Internal helpers that undo a batch of appended document objects without allocating, for multi-object operations (fragment insertion) that must be all-or-nothing |
| `CypherGeometry_DocumentSurfaces` | The geometry document's patch and heightfield stores, so curved patches and terrain live, save, snapshot, and cook alongside brushes and meshes |
| `CypherGeometry_Fragment` | Geometry fragments: a detached set of authored objects (brushes with their surface records, meshes, patches, heightfields) for copy, paste, duplicate, and moving geometry between... |
| `CypherGeometry_MeshDelta` | Undo/redo records for mesh sources: added, removed, or replaced, each carrying canonical descriptions of the mesh before and/or after the change |
| `CypherGeometry_MeshSerialization` | The mesh section of the geometry document format (schema version 3): writing document meshes into, and reading them back from, the CYKV tree the document serializer owns |
| `CypherGeometry_MeshSetDelta` | Ordered mesh-set undo/redo records |
| `CypherGeometry_MeshTransaction` | Mesh edit transactions (begin / edit working copy / commit or cancel) and the one-shot publish commands for adding and removing whole meshes, all producing undoable mesh deltas |
| `CypherGeometry_ReplacementIdentity` | Atomic source-identity planning for brush replacement |
| `CypherGeometry_Snapshot` | Immutable deep-frozen geometry document snapshots |
| `CypherGeometry_SurfaceSerialization` | The schema-4 "patches" and "heightfields" sections of the geometry document format |
| `CypherGeometry_Transaction` | The begin/preview/commit/cancel transaction protocol |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Document

#### Owns

The geometry-local aggregate store: representation pools, source-ID registry,
validated numerical/complexity policy, monotonic revision, immutable snapshots,
and checked publication of committed geometry state.

Snapshots keep referenced storage alive, expose only immutable representation
views, and retain the source revision plus a Core/Document-owned policy version
or hash required by validation, spatial queries, serialization, and Cook. Cook
derives and owns product dependency keys from those inputs. The mutable store has
one writer.

#### Does not own

Mason or CypherTileEditor scene objects, hierarchy, layers, entities, materials,
global selection, editor-wide history, autosave, jobs, renderer resources, or
input routing. Host adapters map scene object IDs to geometry source IDs.

#### First acceptance gate

Publish an immutable snapshot of one validated brush, commit a revisioned change,
and prove the older snapshot remains unchanged. Stale revision, cancellation,
validation failure, and allocation failure must publish nothing.

### Transactions

#### Owns

Preview/commit/cancel state, mutation journals, invertible deltas, element remaps, provenance, checkpoints, rollback, and geometry-local undo payloads.

#### Does not own

The editor-wide command history spanning entities, materials, settings, and documents.

#### First acceptance gate

Repeated preview replacement of one canonical brush value collapses into one
commit and one inverse delta; cancel, invalid input, stale revision, and
allocation failure restore the exact authored state. Real face-drag integration
belongs to the later brush-edit gate.

### Serialization

#### Owns

Versioned authored-geometry schemas, explicit stable wire discriminants,
stable-ID persistence, deterministic readers/writers, migration, bounds, and
corrupt-input diagnostics. Raw C++ enum ordinals, COUNT sentinels, and live
handles are never serialized.

#### Does not own

TileEditor `.cymap` document semantics or cooked runtime formats.

#### First acceptance gate

Round-trip the first brush/document source byte-deterministically and reject
malformed data within fixed budgets. Every later source representation must add
its explicit wire schema and migration coverage before that representation's
delivery gate closes.

### Exchange

#### Owns

Neutral fragments for extract, insert, clone, duplicate, clipboard, and
cross-document source-ID remapping. `Sanitation` orchestrates bounded neutral
input through Intermediates, Validation, and checked representation builders.

#### Does not own

File-format parsing policy or authoritative representation invariants.

#### First acceptance gate

Clone a mixed representation fragment into a new identity domain with no collisions and complete provenance.
