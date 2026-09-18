# Serialization

## Owns

Versioned authored-geometry schemas, stable-ID persistence, deterministic readers/writers, migration, bounds, and corrupt-input diagnostics.

## Does not own

TileEditor `.cymap` document semantics or cooked runtime formats.

## First acceptance gate

Round-trip every source representation byte-deterministically and reject malformed data within fixed budgets.
