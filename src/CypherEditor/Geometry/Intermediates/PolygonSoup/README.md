# Intermediates / PolygonSoup

## Owns

Bounded polygon faces, corner streams, source provenance, and optional grouping
without promising authoritative manifold adjacency.

## Does not own

EditableMesh invariants, automatic welding, semantic object identity, or silent
publication as authored geometry.

## First acceptance gate

Round-trip bounded disconnected polygons, diagnose invalid rings and non-finite
coordinates, and convert only validated connected components through an explicit
result and remap.
