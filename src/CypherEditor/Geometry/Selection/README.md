# Selection

## Owns

Representation-qualified geometry component references, selectable-target views,
sets, grow/shrink, connected, loop/ring, and mutation-remap behavior.

## Does not own

Internal topology-handle identity, global selection routing, Qt models, gizmo
state, TileEditor cells, or non-geometry scene objects.

## First acceptance gate

Keep face, edge, and vertex selections stable across a split/merge remap with explicit ambiguity reporting.
