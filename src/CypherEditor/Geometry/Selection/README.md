# Selection

## Owns

Geometry component references, sets, grow/shrink, connected, loop/ring, and mutation-remap behavior.

## Does not own

Global selection routing, Qt models, gizmo state, or non-geometry scene objects.

## First acceptance gate

Keep face, edge, and vertex selections stable across a split/merge remap with explicit ambiguity reporting.
