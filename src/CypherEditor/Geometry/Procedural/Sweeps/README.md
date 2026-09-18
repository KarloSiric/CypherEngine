# Procedural / Sweeps

## Owns

Pure profile-along-path, lathe, and loft evaluation with seam, frame,
tessellation, and provenance policy. It evaluates supplied parameters; it does
not decide whether those parameters are transient command inputs or retained by
a Modifier/source recipe.

## Does not own

Arbitrary Boolean cleanup.

## First acceptance gate

Sweep one profile without frame flips and retain profile/path provenance.
