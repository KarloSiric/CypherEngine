# Non-Renderer Validation — 2026-09-16

## Scope and result

NR-01 is complete: dedicated correctness coverage and a focused performance
baseline now exist for the runtime `Mem_Pool*` allocator. The tests exercise
`src/CypherMemory`, not Common Tier1's separate `memory_pool_t` implementation.
No allocator implementation defect was exposed by this slice, and no runtime
source was changed by this work.

The separate `cypher_memory_pool_tests` CTest executable passed in Debug and
ASan/UBSan: **8 test cases and 58,552 assertions in each configuration**. The
assertion count mostly comes from checking every live slot during deterministic
churn; it does not represent that many distinct scenarios. Undefined-behavior
and address-sanitizer errors were configured to stop the test.

This was a focused verification. The complete engine suite was not rerun by
this task. The historical 235-test Debug result in the changelog is dated
September 15 and is separate from this result.

## Coverage

- External backing: misaligned starting span, aligned unique slots, untouched
  guard bytes, capacity exhaustion, reuse, shutdown clearing, and retained
  caller ownership.
- Arena backing: pool shutdown leaves the parent initialized at its existing
  cursor; surrounding arena allocations remain intact; exhausted-arena pool
  initialization preserves existing allocations and backing bytes.
- Rejected operations: foreign/interior/null/double frees, invalid descriptors,
  insufficient storage, overflow, invalid allocation sizes/alignments, and
  repeated initialization. Failures preserve live data and available capacity.
- Lifetime and diagnostics: counter reset with live allocations, bulk reset,
  peak/current counters, zero-allocation variants, and successful retry.
- Bitmap boundaries: slots spanning three 64-bit allocation words, followed by
  384 reproducible mixed allocation/free operations with live-payload checks.

The suite deliberately does not require a specific free-list order or read
released payloads. Pool raw pointers do not promise generation-based stale
pointer detection after the same address has been allocated again. Dedicated
arena backing/marker/virtual-memory coverage remains NR-02.

## Reproduction

Run from the repository root:

```sh
cmake --build --preset debug --target cypher_memory_pool_tests -j 2
ctest --preset debug -R '^cypher_memory_pool_tests$' -V

cmake --build --preset asan-ubsan --target cypher_memory_pool_tests -j 2
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ASAN_OPTIONS=halt_on_error=1 \
  ctest --preset asan-ubsan -R '^cypher_memory_pool_tests$' -V

cmake --build --preset bench-release --target cypher_memory_bench -j 2
mkdir -p out/validation/non-renderer-2026-09-16
./out/build/bench-release/bin/cypher_memory_bench \
  --benchmark_filter='^BM_Pool_AllocFree64' \
  --benchmark_min_time=0.2s --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=out/validation/non-renderer-2026-09-16/pool-release.json \
  --benchmark_out_format=json
```

Configure the corresponding CMake preset first if its build tree does not exist.

## Release pool benchmark baseline

The performance question is whether fixed-slot allocator throughput changes as
the working set grows and frees occur in shuffled order. Each iteration fills
the pool and frees every slot. The permutation is seeded and generated before
timing; the same permutation is reused. Caller payloads are not processed.
Every allocation/free failure now reports an error instead of contributing a
misleading successful timing.

Measured locally on Apple Silicon macOS, Release, 64-byte payloads, five
repetitions at a minimum of 0.2 seconds per repetition:

| Workload | Slots | Median CPU ns per allocation or free | Median million operations/s |
| --- | ---: | ---: | ---: |
| Sequential free | 1,024 | 4.713 | 212.19 |
| Shuffled free | 64 | 4.605 | 217.17 |
| Shuffled free | 1,024 | 4.524 | 221.04 |
| Shuffled free | 16,384 | 5.916 | 169.02 |

One operation is an allocation **or** a free. Batch CPU time is divided by
`2 * slot_count`; raw batch durations cannot be compared across capacities.
The 1,024-slot comparison also includes the shuffled case's extra permutation
reads, so it cannot isolate cache effects. These full-pool batches do not model
mixed gameplay lifetimes or concurrent access.

The machine reported substantial background load (about 18 one-minute load
average), and the benchmark runner could not set affinity or retrieve the CPU
clock rate. Its displayed CPU-frequency metadata is therefore not reliable.
Treat these numbers as a reproducible workload baseline, not an optimization
claim or performance gate. The larger shuffled working set costs more in this
run; attributing that difference requires a quieter comparison and profiling.

## Local evidence and next step

Ignored local evidence lives under `out/validation/non-renderer-2026-09-16/`:
`pool-debug.log`, `pool-asan-ubsan.log`, `pool-release.json`, and the preserved
historical `historical-debug-2026-09-15.log`. CTest's shared `LastTest.log` can be
overwritten by another task, so the new checks use separate output logs.

This report preserves the initial NR-01 slice. Subsequent arena, resource,
filesystem, package, command/config and logging work is recorded in the
[runtime work log](non_renderer_work_log.md); the
[non-renderer queue](six_month_engine_plan.md#non-renderer-work-queue) tracks
remaining scope. Input remains a design candidate.
