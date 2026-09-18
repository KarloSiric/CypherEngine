# Benchmarks Tests

This directory records benchmark scenarios and regression reproductions beside
the Geometry test plan. Executable performance benchmarks live under
`benchmarks/CypherEditor/Geometry/` so they are never confused with correctness
tests or run implicitly through the normal CTest suite.

Add the first benchmark target only when there is a real algorithm to measure.
Each case must state its data size, seed or fixture version, warm-up policy,
measured operation, scratch budget, and reported units. Planned subjects are
large transactional edits, validation, editable-BVH updates, CSG stages,
tessellation, serialization, and cook throughput.
