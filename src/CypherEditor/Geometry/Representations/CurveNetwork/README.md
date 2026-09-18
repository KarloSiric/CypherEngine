# Representations / CurveNetwork

## Owns

Persistent editable curves, network connectivity, basis-specific control data,
stable source identity, parameter domains, and checked immutable views used by
sweeps, lofts, rails, roads, pipes, and other retained path geometry.

## Does not own

Camera, scripted-sequence, entity, traffic, or gameplay-path semantics. A host
may reference a CurveNetwork from those systems without moving their ownership
into Geometry.

## First acceptance gate

Store and evaluate one connected cubic curve path, split it without changing its
shape, and preserve source identity and parameter provenance through round-trip
serialization.
