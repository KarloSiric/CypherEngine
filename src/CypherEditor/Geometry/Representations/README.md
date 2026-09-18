# Representations

## Owns

Canonical BrushSolid, EditableMesh, PlanarRegion, PatchSurface, CurveNetwork,
and HeightField source models, plus rules for explicit conversion with
provenance. Neutral soup storage belongs to `Intermediates`.

## Does not own

A universal topology. Brush planes, manifold meshes, planar regions, parametric
surfaces, retained curves, height samples, and raw soup retain different
invariants.

## First acceptance gate

Expose immutable representation views and checked conversion entry points without losing source IDs.
