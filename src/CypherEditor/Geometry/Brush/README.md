# Brush

The plane-defined convex brush: its canonical solid, boundary, validation and document source, and every brush-level edit - construction, shapes, sweep, transform, clip, vertex manipulation, brush CSG and the orchestrated edit pipeline.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_BrushAuthoring` | Authored-brush edits that create or reshape brushes while carrying their surfaces (material + UV projection) along: the extrude tool's split mode and mirroring |
| `CypherGeometry_BrushBoundary` | Derived boundary geometry for a plane-defined brush |
| `CypherGeometry_BrushCSG` | Brush-level constructive solid geometry operations |
| `CypherGeometry_BrushClip` | Failure-atomic plane clipping and slicing for brushes |
| `CypherGeometry_BrushEditPipeline` | The orchestrated brush edit workflow |
| `CypherGeometry_BrushGenerator` | Pure primitive generators that produce brush solids |
| `CypherGeometry_BrushPatches` | "create patches from brush faces": turning a brush face into flat Bezier patches that cover it exactly, as the starting point for curved detail |
| `CypherGeometry_BrushShapes` | Box-fitted brush shapes for the draw-shape tool |
| `CypherGeometry_BrushSolid` | The canonical plane-defined convex solid and its side management interface |
| `CypherGeometry_BrushSource` | The document-level authored brush ownership unit |
| `CypherGeometry_BrushSweep` | The sweep tool: fill the gap between selected brush faces and a copy of them moved along a path (straight, arc, or S-bend), with a number of segments and repeated iterations |
| `CypherGeometry_BrushTransform` | Affine transform operations on brush side planes |
| `CypherGeometry_BrushValidation` | Quick and deep validation for plane-defined brushes |
| `CypherGeometry_BrushVertexClump` | The multi-brush vertex move ("vertex clumping") |
| `CypherGeometry_BrushVertexOps` | Vertex-level manipulation operations on convex brushes |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Representations / Brush

#### Owns

Convex solids as intersections of oriented planes, stable brush-side identity, side attributes, texture projection, and reconstructable boundary caches.

#### Does not own

Arbitrary concave or non-manifold meshes. A brush boundary mesh is derived from its plane set.

#### First acceptance gate

Construct a six-plane box, reject contradictory or unbounded plane sets, and reconstruct an outward watertight boundary.

### Operations / Transform

#### Owns

Translate, rotate, scale, shear, reflect, and pivot-relative transforms over checked component sets.

#### Does not own

Viewport coordinate-space UI or manipulator rendering.

#### First acceptance gate

Transform a mixed selection deterministically with local/world policy and exact undo.

### Operations / Cutting

#### Owns

Clip-by-plane, slice, bisect, and trim operations with optional explicit caps and provenance.

#### Does not own

Automatic fan capping of arbitrary boundaries.

#### First acceptance gate

Split a convex brush by a plane into valid outputs with stable side/source mapping.
