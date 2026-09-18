# Core

## Owns

Shared identity, document-local source-ID registration and deterministic transfer
remapping, persistent-source taxonomy, representation-qualified aliases over the
Common wide generation pool, bounded structured diagnostics, allocation
boundaries, scratch budgets, and hard complexity limits.

## Does not own

Geometric predicates, topology storage, documents, UI commands, or renderer state.

## First acceptance gate

Allocate and claim persistent source IDs monotonically, preserve retired claims,
restore them only through an explicit undo path, remap transferred IDs
deterministically, reject invalid policies, report diagnostics without hidden
allocation, and prove that bounded scratch and Common pool growth are
failure-atomic while stale live handles cannot resolve after removal, clear,
slot reuse, or terminal-generation retirement.
