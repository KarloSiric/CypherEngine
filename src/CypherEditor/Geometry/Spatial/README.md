# Spatial

## Owns

Editable BVH, derived triangle caches, local refit/rebuild, component picking, overlap, snapping candidates, and dirty-region tracking.

## Does not own

A second octree merely for feature parity, renderer acceleration structures, or input routing.

## First acceptance gate

Match brute-force query results after local edits, refits, rebuilds, and snapshot publication.
