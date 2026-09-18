# Attributes / Schema

## Owns

Typed attribute descriptors, domains, opaque material references, UV projection
records, value/storage validation, and bounded storage used by canonical source
representations. Domains include representation root, vertex, corner, edge,
face, brush side, control point, and sample where supported.

## Does not own

Topology-changing propagation, interpolation across edits, material databases,
texture loading, shader binding, or browser UI.

## First acceptance gate

Allocate, copy, query, and validate bounded brush-side material and UV projection
records with deterministic enumeration and failure-atomic growth.
