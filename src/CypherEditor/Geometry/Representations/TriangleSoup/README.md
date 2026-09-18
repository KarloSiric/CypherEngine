# Representations / TriangleSoup

## Owns

Neutral indexed or independent triangles for import, sanitation, CSG intermediates, and diagnostics on data that does not satisfy mesh invariants.

## Does not own

Authoritative manifold adjacency or silent conversion into EditableMesh.

## First acceptance gate

Accept bounded malformed input, diagnose it, and convert only sanitized components into checked representations.
