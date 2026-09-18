# CSG / Brush

## Owns

Regularized Boolean evaluation over validated plane-defined convex brush operands, including clipping, region selection, boundary reconstruction, and side provenance.

## Does not own

Arbitrary closed-mesh corefinement or conversion of concave results back into one convex brush. A result may be a deterministic set of convex brushes or an explicit mesh conversion.

## First acceptance gate

Deliver contained, disjoint, touching, shared-plane, intersection, and subtraction fixtures before brush union decomposition.
