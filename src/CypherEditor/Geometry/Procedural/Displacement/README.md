# Procedural / Displacement

## Owns

Bounded displacement modifier descriptors, sampling, refinement policy, and
source mapping over immutable inputs.

## Does not own

Persistent HeightField tiles, samples, holes, or identity; terrain gameplay
semantics; or sculpt-brush input.

## First acceptance gate

Displace a fixture deterministically within vertex and scratch budgets.
