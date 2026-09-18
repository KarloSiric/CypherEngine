# Attributes / Propagation

## Owns

Operation-specific interpolation, generated-face policy, world-locked versus
geometry-locked texture behavior, smoothing/crease transfer, conflict handling,
and provenance for topology-changing edits, conversion, repair, and CSG.

## Does not own

Base attribute storage, topology mutation, transaction publication, material
asset loading, or presentation.

## First acceptance gate

Move one brush side through a preview transaction while preserving its material,
provenance, and world-locked texture projection; cancel and undo restore the
exact original records.
