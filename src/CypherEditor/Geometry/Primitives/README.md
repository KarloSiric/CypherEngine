# Primitives

## Owns

Deterministic parameter records and generators for planes, boxes, wedges, prisms, pyramids, cylinders, cones, spheres, arches, and stairs.

Initial descriptors are transient pure inputs that emit checked Brush, Mesh,
PlanarRegion, or Patch data. Retaining a creation recipe as an authored source
requires a later explicit `ProceduralRecipe` contract; a parameterized creation
dialog alone does not make the descriptor persistent geometry.

Low-level generators depend only on Kernel and checked representation builders.
An architectural creation command that needs clipping or CSG belongs in the
later operation layer even when its parameter record remains here.

## Does not own

Document mutation, serialization of unapproved retained recipes, or thousands of
hard-coded shape algorithms. Large shape libraries are data-driven templates
built from tested generators.

## First acceptance gate

Generate one canonical six-plane box at valid and rejected parameter boundaries.
Wedge, arch, and stair creation close later architectural-operation gates.
