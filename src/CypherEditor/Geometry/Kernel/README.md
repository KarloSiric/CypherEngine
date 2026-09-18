# Kernel

## Owns

The Geometry-facing numerical contract used by every authored representation:

- double-precision authoring coordinates and planes;
- explicit coordinate-range and finite-value checks;
- filtered predicates with an exact-sign fallback where a wrong sign can alter
  topology;
- checked plane classification and controlled geometric constructions;
- deterministic coordinate quantization and total ordering;
- the small policy facade that converts Geometry tolerances into an explicit
  operation decision.

The primitive vector, plane, and predicate implementations belong in
`Cypher::Math` when they are useful outside the editor. This module owns how the
Geometry runtime applies those primitives. It must never hide a topology choice
behind an undocumented global epsilon.

## Does not own

Topology ownership, document state, repair, user-facing snapping, gizmos, or
tool behavior. `Constraints/` owns snapping and constraint solving. A
representation operation owns the decision it makes from a predicate result.

## Floating-point contract

Predicate translation units require strict floating-point behavior: no
fast-math, reassociation, or implicit contraction that can change a sign. The
supported environment assumes round-to-nearest and gradual underflow. Any
third-party exact-predicate algorithm must record its source and license before
code is adapted.

## First implementation slices

1. Add `vec2d_t` and `vec3d_t` beside the existing float vector APIs in
   `Cypher::Math`, with finite checks and checked length/normalization.
2. Add `plane3d_t` and checked construction/classification.
3. Add orientation predicates and the Geometry policy facade.
4. Add authoring-coordinate quantization as a separate reviewed slice.

Each slice lands with focused contract tests before a dependent Geometry type
is allowed to use it.

## First acceptance gate

Classify orientation and plane sidedness deterministically at documented scale
limits without a global epsilon. Boundary tests must include `nextafter`
neighbors, reversed winding, very small and very large finite inputs, NaN and
infinity rejection, and repeatability across Debug, Release, and sanitizer
builds.
