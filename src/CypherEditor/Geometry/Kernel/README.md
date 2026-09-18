# Kernel

## Owns

Double-precision authoring policy, coordinate quantization, filtered/exact-fallback predicates, controlled constructions, plane classification, and deterministic ordering.

## Does not own

Topology ownership or user-facing snapping/tool behavior. Representation-independent primitive predicates should move down to Cypher::Math.

## First acceptance gate

Classify orientation and plane sidedness deterministically at documented scale limits without a global epsilon.
