# Representations / HeightField

## Owns

Persistent bounded height samples, tile/sample identity, hole masks, geometry
bounds, deterministic sampling, and conversion into validated surface geometry.

## Does not own

Terrain biomes, paint layers, foliage, streaming policy, gameplay tags, physics
objects, or navigation behavior. Those systems consume geometry snapshots or
source mappings through adapters.

## First acceptance gate

Store a small tiled height field with one hole, edit a bounded sample region, and
tessellate only the dirty tiles with deterministic boundary stitching and source
mapping.
