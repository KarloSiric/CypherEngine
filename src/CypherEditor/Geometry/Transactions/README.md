# Transactions

## Owns

Preview/commit/cancel state, mutation journals, invertible deltas, element remaps, provenance, checkpoints, rollback, and geometry-local undo payloads.

## Does not own

The editor-wide command history spanning entities, materials, settings, and documents.

## First acceptance gate

Repeated preview replacement of one canonical brush value collapses into one
commit and one inverse delta; cancel, invalid input, stale revision, and
allocation failure restore the exact authored state. Real face-drag integration
belongs to the later brush-edit gate.
