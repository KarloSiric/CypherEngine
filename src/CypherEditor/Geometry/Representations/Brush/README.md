# Representations / Brush

## Owns

Convex solids as intersections of oriented planes, stable brush-side identity, side attributes, texture projection, and reconstructable boundary caches.

## Does not own

Arbitrary concave or non-manifold meshes. A brush boundary mesh is derived from its plane set.

## First acceptance gate

Construct a six-plane box, reject contradictory or unbounded plane sets, and reconstruct an outward watertight boundary.
