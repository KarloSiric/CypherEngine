# Document

## Owns

The geometry-local aggregate store: representation pools, source-ID registry,
validated numerical/complexity policy, monotonic revision, immutable snapshots,
and checked publication of committed geometry state.

Snapshots keep referenced storage alive, expose only immutable representation
views, and retain the source revision plus a Core/Document-owned policy version
or hash required by validation, spatial queries, serialization, and Cook. Cook
derives and owns product dependency keys from those inputs. The mutable store has
one writer.

## Does not own

Mason or CypherTileEditor scene objects, hierarchy, layers, entities, materials,
global selection, editor-wide history, autosave, jobs, renderer resources, or
input routing. Host adapters map scene object IDs to geometry source IDs.

## First acceptance gate

Publish an immutable snapshot of one validated brush, commit a revisioned change,
and prove the older snapshot remains unchanged. Stale revision, cancellation,
validation failure, and allocation failure must publish nothing.
