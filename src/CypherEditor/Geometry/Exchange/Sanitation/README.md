# Exchange / Sanitation

## Owns

Bounded orchestration from neutral PolygonSoup, TriangleSoup, and versioned
exchange records into checked canonical source representations. It requests
Validation diagnostics, applies explicit caller-selected sanitation policy, and
publishes complete source/remap/provenance records through checked builders.

## Does not own

File-format parsing, silent geometric repair, host object identity, material
asset loading, or relaxed invariants in canonical Brush or EditableMesh storage.
Intermediates own raw records; Validation reports faults; Repair owns explicit
undoable source changes after import.

## First acceptance gate

Accept a bounded valid polygon soup into a checked representation and reject
non-finite, duplicate, non-manifold, over-budget, and ambiguous input without
partially publishing source elements.
