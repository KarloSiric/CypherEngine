# Spatial

## Owns

Source/component bounds, editable BVH, local refit/rebuild, component picking,
overlap, snapping candidates, and dirty-region tracking. Derived triangle pick
caches are admitted only after the corresponding Tessellation contract exists;
they retain source mapping and snapshot revision.

## Does not own

A second octree merely for feature parity, renderer acceleration structures, or input routing.

## First acceptance gate

Match brute-force query results after local edits, refits, rebuilds, and snapshot publication.
