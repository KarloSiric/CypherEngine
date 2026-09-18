# Transactions

## Owns

Preview/commit/cancel state, mutation journals, invertible deltas, element remaps, provenance, checkpoints, rollback, and geometry-local undo payloads.

## Does not own

The editor-wide command history spanning entities, materials, settings, and documents.

## First acceptance gate

Repeated drag updates collapse into one commit; invalid or allocation-failed edits restore the byte-equivalent authored state.
