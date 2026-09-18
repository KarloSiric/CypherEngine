# Cook / DependencyGraph

## Owns

Deterministic dependencies from source revisions and policy hashes to immutable
cook products, plus dirty propagation and reusable product keys.

## Does not own

Background-job scheduling, file watching, build orchestration, autosave, or live
editor IPC.

## First acceptance gate

Change one bounded brush attribute, invalidate exactly the affected products,
and retain byte-identical keys for every unaffected product.
