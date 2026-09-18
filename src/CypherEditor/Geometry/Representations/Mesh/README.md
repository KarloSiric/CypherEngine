# Representations / Mesh

## Owns

Editable oriented polygonal two-manifold storage: mesh, shell, vertex, half-edge, edge, loop, and face pools plus deterministic traversal.

## Does not own

Malformed imports, convex brush plane sets, renderer vertex buffers, or non-manifold Boolean intermediates.

## First acceptance gate

Build the canonical closed box with 8 vertices, 12 edges, 24 half-edges, 6 loops, 6 faces, one shell, and Euler characteristic 2.
