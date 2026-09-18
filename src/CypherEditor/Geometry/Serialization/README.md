# Serialization

## Owns

Versioned authored-geometry schemas, explicit stable wire discriminants,
stable-ID persistence, deterministic readers/writers, migration, bounds, and
corrupt-input diagnostics. Raw C++ enum ordinals, COUNT sentinels, and live
handles are never serialized.

## Does not own

TileEditor `.cymap` document semantics or cooked runtime formats.

## First acceptance gate

Round-trip every source representation byte-deterministically and reject malformed data within fixed budgets.
