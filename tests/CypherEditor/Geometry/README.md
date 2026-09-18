# Cypher Editor Geometry Tests

Tests follow public behavior and invariants rather than source-file count. Every mutation fixture validates before and after, checks its exact remap/provenance, applies the inverse delta, and compares the restored authored state. Fuzz and benchmark inputs must obey explicit time, element, diagnostic, and scratch budgets.
