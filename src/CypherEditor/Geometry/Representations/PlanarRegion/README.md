# Representations / PlanarRegion

## Owns

Persistent planar regions containing one or more polygons, outer contours,
holes, stable component identity, and plane-frame provenance.

## Does not own

3D shell adjacency or triangulated render output.

## First acceptance gate

Represent a disconnected region containing a polygon with a hole, validate
winding and containment, and round-trip it without losing polygon or contour
identity.
