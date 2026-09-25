# Cypher Editor Geometry Tests

Tests follow public behavior and invariants rather than source-file count. Every mutation fixture validates before and after, checks its exact remap/provenance, applies the inverse delta, and compares the restored authored state. Fuzz and benchmark inputs must obey explicit time, element, diagnostic, and scratch budgets.

Each implementation snippet adds the smallest focused contract test that can
prove its public behavior. Deterministic generated cases use fixed, named seeds
and report their input on failure. Golden files begin only after a canonical
serializer or cook product exists; fuzz targets begin only at a real byte or
operation-stream boundary; executable benchmarks live in
`benchmarks/CypherEditor/Geometry/` and are opt-in.

Test folders mirror the ten source folders under
[`src/CypherEditor/Geometry/`](../../../src/CypherEditor/Geometry/README.md), so
a header's tests live in the folder of the same name. The representation
folders (Brush, Mesh, Surfaces) cover canonical invariants, builders, and
explicit conversion fixtures; `Fuzz/`, `Golden/`, and `Benchmarks/` hold
cross-cutting plans rather than per-folder tests.
