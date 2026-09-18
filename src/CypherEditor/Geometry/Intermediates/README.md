# Intermediates

## Owns

Bounded neutral records used between import, validation, repair, planar,
tessellation, and CSG stages. These records may temporarily lack authored
adjacency or contain geometry that has not passed representation invariants.

## Does not own

Persistent authored objects, semantic scene volumes, or cooked runtime assets.
Intersection graphs, corefined faces, classified cells, and boundary fragments
remain private to the operation pipeline that defines their invariants.

## First acceptance gate

Accept bounded triangle and polygon soup, preserve input provenance, emit stable
diagnostics for malformed data, and publish a source representation only after
an explicit checked conversion succeeds.
