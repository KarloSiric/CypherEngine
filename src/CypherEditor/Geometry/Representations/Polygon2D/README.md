# Representations / Polygon2D

## Owns

Planar contours, outer loops, holes, plane-frame provenance, and neutral region records used by floors, faces, and coplanar processing.

## Does not own

3D shell adjacency or triangulated render output.

## First acceptance gate

Represent one polygon with a hole, validate winding and containment, and round-trip it without losing contour identity.
