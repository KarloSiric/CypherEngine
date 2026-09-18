# Tessellation

## Owns

Deterministic conversion of brushes, polygon faces with holes, patches, and approved soup into indexed triangles with provenance.

## Does not own

Render batching, GPU upload, or authored source replacement.

## First acceptance gate

Tessellate the brush box to exactly 12 outward triangles and map every triangle to one source side.
