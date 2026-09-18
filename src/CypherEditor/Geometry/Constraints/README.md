# Constraints

## Owns

Pure deterministic resolution of grid, angle, axis, vertex, edge, face, surface,
normal-direction, and user-defined geometry constraints from bounded candidate
sets. Ranking declares distance spaces, priority, stable tie-breaking, and the
exact result/provenance used by preview transactions.

Snapping changes a proposed coordinate or transform. It never implies a weld or
any other topology mutation.

## Does not own

Pointer gestures, active workplane/tool state, viewport pixels, hotkeys, gizmo
rendering, sticky UI behavior, or candidate discovery. The host supplies mode
and gesture context; Spatial supplies geometry candidates.

## First acceptance gate

Resolve bounded grid and component candidates idempotently with deterministic
negative-half and equal-distance tie behavior across input permutations.
