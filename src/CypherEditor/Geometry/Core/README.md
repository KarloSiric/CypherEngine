# Core

## Owns

Shared identity, result vocabulary, generation-handle contracts, allocation boundaries, scratch budgets, and hard complexity limits.

## Does not own

Geometric predicates, topology storage, documents, UI commands, or renderer state.

## First acceptance gate

Allocate persistent source IDs monotonically, reject invalid policies, and prove stale live handles cannot resolve after slot reuse.
