# Non-renderer runtime work — 2026-09-16

## Scope

The author owns the renderer. This parallel track improves currently implemented
runtime systems through bounded fixes, regression tests, and relevant benchmarks.
It does not start placeholder World, Physics, Audio, or Input implementations.
Renderer and cooked-shader edits in the shared checkout belong to other work.

This implementation and validation pass is complete. All 232 selected
non-renderer CTest entries pass in both Debug and ASan/UBSan, and the three
selected ThreadSanitizer suites pass. Build trees are isolated under
`out/build/nonrenderer-*`; evidence is preserved under
`out/validation/non-renderer-2026-09-16/` because another task can overwrite the
usual shared CTest log.

## Completed focused checks

| Area | Change / coverage | Verification so far |
| --- | --- | --- |
| Runtime pool | External/arena ownership, overflow, invalid frees, exhaustion, bitmap boundaries, deterministic churn; shuffled-free benchmark | Debug + ASan/UBSan, 8 cases / 58,552 assertions. Initial Release measurements in the [pool report](non_renderer_validation_2026-09-16.md). |
| Runtime arena | Clear all used bytes before decommit; reset stale error/trace diagnostics; correct first trace index. Marker, alignment, backing, zeroing and virtual-memory lifecycle tests | Debug + ASan/UBSan, 10 cases / 421 assertions. The old reset behavior failed the new real-VM regression on macOS. |
| Scratch / bucket | Preserve ScratchEnd's rewind error after clearing the scope; nested scope, bucket rollback, fallback, invalid-free and ownership tests | Debug + ASan/UBSan, 8 cases / 305 assertions. No bucket implementation change was needed. |
| Allocator thread wrappers | Keep binding checks, operation, last-error publication and return-value read under one mutex; synchronize bind/unbind and const error getters | Debug + ASan/UBSan + TSan, 7 cases / 1,865 assertions. |
| Global memory frontend | Seven distinct arena routes, rollback at each initialization stage, frame boundaries, copied config and tag accounting; clarify sampled aggregate peak versus summed arena peaks | Debug + ASan/UBSan, 6 cases / 1,856 assertions. Production comments only; no behavior change was required. |
| Resource | Nested dependency teardown, failed-load cleanup/retry, callback reentrancy rejection, null-payload rollback, wrong-type handles and allocator accounting | Debug + ASan/UBSan, 16 total cases / 3,965 assertions. Six new cases; no resource implementation fix was required. |
| Command | Reject overlong input before callback dispatch; clear argc on parser failure; reject null argv | Debug + ASan/UBSan, 5 cases / 302 assertions. The old code executed a silently truncated command. |
| Config / CVar | Reject truncated values/tokens, extra arguments and embedded NUL files; bound nested exec depth; preserve aliased CVar input; use defined-range numeric parsing | Debug + ASan/UBSan, 6 cases / 137 assertions. |
| Pak | Validate declared payload region, path lengths and compression consistency; preserve owned readers/writers on reopen; publish via exclusive sibling temporary file and final replacement | Debug + ASan/UBSan smoke passed, including malformed fixtures, forced partial-write preservation and replacement cleanup. |
| FileSystem | Keep cancellation nonterminal while workers borrow buffers; reuse finished cancelled slots; preserve file ownership on reopen; close async admission before shutdown drains | Debug + ASan/UBSan + TSan smoke passed, including promise-controlled and lock-controlled concurrency regressions. |
| Log | Validate paths/policies before file operations; defer candidate truncation until all sinks open; flush active aliases before truncation; mark producer-side message truncation; provide a quiet terminal presentation without changing detailed file records | Debug + ASan/UBSan + TSan, 10 total cases / 191 assertions. Coverage includes console INFO/warning/fatal/color formatting, oversized messages, malformed config, failed init/update and successful alias truncation. |
| Startup diagnostics | Expanded the normal INFO startup stream into ordered product/build, System/process, Memory, core-service, FileSystem, Command, Config/CVar, Log, runtime-policy, SDL/window/display, I/O, and timing sections. Every live arena, mount, command, CVar, configurable log sink, relevant path, and actual window/display state is retained. The terminal presentation uses plain INFO text, fixed 80-column section rules, aligned parent/child rows, and wrapped descriptions; warnings and errors retain severity/channel metadata and color. | Main engine target built, live startup/shutdown capture completed, and the final source passed all 232 selected tests in both Debug and ASan/UBSan plus the Log suite under TSan. The capture exits cleanly with 304 lines, no ANSI sequences or `[INFO]` prefixes, a 119-column maximum, and no line wider than 120 columns. |

## Consolidated results

- Final Debug: **232/232 CTest entries pass**, including the global memory,
  cancellation-slot, registry-diagnostic, platform-backend, and startup-report
  dependencies.
- Final ASan/UBSan: **232/232 CTest entries pass**. Both sanitizers halt on the
  first error.
- Final ThreadSanitizer: **3/3 suites pass**: allocator wrappers, Log and FileSystem.
- Release: **33 workloads across five executables**, five repetitions each, no
  reported workload errors. Runtime memory was rebuilt and rerun after the final
  arena diagnostics change.

## Expanded live startup report

The normal terminal startup now uses a dedicated console presentation for INFO
records. Routine startup text is printed without repeated `[INFO][CHANNEL]`
prefixes, while warnings and errors keep severity/channel metadata and terminal
color. Fixed 80-column section rules separate major phases; aligned parent/child
rows make ownership and hierarchy visible; and long descriptions wrap beneath
their fields instead of producing unreadable single-line records.

The presentation change does not discard diagnostic coverage. The report still
contains every live global arena, mounted root, registered command, effective
CVar, configurable stream sink, relevant path, actual SDL/window/display state,
and both core-service and end-to-end timings. Detailed file sinks continue to
carry their normal record metadata.

The report exposed several values that had previously looked valid while being
wrong. macOS available physical memory now comes from Mach VM statistics instead
of zero; nonnumeric CVar text such as `info` now has a zero float cache instead
of infinity; optional config loading suppresses only a missing path; and compact
log formatting now honors its timestamp flag. The report also states that the
crash buffer is reserved but unimplemented and that console/editor/game files
require an explicit record route.

The renderer was not changed. `RenderArena` remains visible because it is one of
the Memory subsystem's seven reservations and is included in the aggregate
4.44 GiB budget. The window record truthfully reports `graphics_api=none` for
this Host run.

The final presentation capture is
`out/validation/non-renderer-2026-09-16/startup-format-final-live.log`. It
contains 304 lines, exits cleanly, contains zero ANSI escape sequences and zero
`[INFO]` prefixes, has a maximum rendered width of 119 columns, and has no line
wider than 120 columns. The same final source passed all 232 selected tests in
Debug and ASan/UBSan; Log also passed its 10-case, 191-assertion ThreadSanitizer
run. The detailed file-sink capture is
`startup-format-final-engine-detailed.log` in the same evidence directory.

## Contracts and limits

- Allocator wrappers protect calls through one wrapper. Borrowed allocator and
  backing lifetime, direct allocator access, and returned payload lifetime still
  require caller coordination.
- Arena tests exercise real successful VM operations and fixed commitment limits;
  they do not inject every operating-system reserve/commit/decommit failure.
- Config, Command and CVar remain single-threaded runtime services. Parsing limits
  reject invalid input; a config file is not an all-or-nothing transaction.
- Package publication preserves an existing destination on write or replace
  failure. It does not claim crash-durable directory publication.
- Performance numbers on the busy development machine are local baselines, not
  optimization claims or regression gates.
- Log preparation preserves existing bytes on candidate-open failure. Final
  multi-file truncation is not atomic, and failed preparation can leave newly
  created empty candidate files.
- Filesystem request IDs restart on reinitialization. Callers must discard all old
  handles and finish lifecycle coordination before starting a new filesystem
  lifecycle; stronger cross-lifecycle identities remain a separate follow-up.
- Validation was performed on macOS. Windows/Linux execution and injected OS
  failure coverage are not claimed by these results.

## Evidence

`core-sanitize.log` covers Resource, pool, arena, Command, Config and Pak.
`runtime-debug.log` covers scratch/bucket, thread wrappers, current Config and
final package publication. `runtime-sanitize.log` covers scratch/bucket and
thread wrappers. `config-arena-debug.log` records the initial passing arena/config
fixes. `arena-debug-initial.log`, `command-before-fix.log` and
`config-before-fix.log` preserve reproduced failures before the corresponding fixes.

The historical full 235-test Debug result is dated September 15. It is not
evidence for these September 16 changes. The expanded-report validation is in
`expanded-startup-full-debug.log`, `expanded-startup-full-asan-ubsan.log`, and
the final incremental logs in the same evidence directory. The live report is
preserved separately so its structure and values can be reviewed without
restarting the engine. `expanded-startup-validation-summary.json` records the
selection, exit codes, elapsed times, benchmark coverage, smoke-test facts, and
hashes of the validated startup-report source files.

The final presentation rerun is recorded in
`startup-format-final-debug-validation.log`,
`startup-format-final-asan-ubsan-validation.log`,
`startup-format-final-tsan-log.log`, and `startup-format-final-summary.json`.

## Reproducing this checkout's validation

The saved `expanded-startup-selected-tests.json` records the exact 232 selected
tests, 225 executable targets (including process-test helpers), and 14
exclusions. It is a snapshot of this checkout, not automatic discovery of future
test registrations. The exclusions are the renderer contracts, render-resource
integration, renderer-linked cooked-format suites, render preview, OpenGL
context, and renderer shader/draw/runtime tests. This leaves Common Tier0/1/2,
Math, Security, Image, ToolFramework, Resource/VFS, System runtime/window/events/
process output, and all runtime suites in the selection.

From the repository root, after configuring the isolated Debug tree:

```sh
python3 - <<'PY'
import json, subprocess
from pathlib import Path
evidence = Path('out/validation/non-renderer-2026-09-16')
selection = json.loads((evidence / 'expanded-startup-selected-tests.json').read_text())
subprocess.run(['cmake', '--build', 'out/build/nonrenderer-debug', '--target',
                *selection['build_targets'], '-j', '2'], check=True)
subprocess.run(['ctest', '--test-dir', 'out/build/nonrenderer-debug',
                '-R', selection['include_regex'], '--output-on-failure', '-j', '2'],
               check=True)
PY
```

The Debug and sanitizer trees use the `debug` and `asan-ubsan` presets with
`-B out/build/nonrenderer-debug` / `-B out/build/nonrenderer-sanitize`. This run
reused each preset's existing `vcpkg_installed` directory with
`VCPKG_MANIFEST_INSTALL=OFF`; a fresh machine should resolve those dependencies
normally. The separate TSan tree uses Debug with `-fsanitize=thread` compiler and
linker flags plus `-fno-omit-frame-pointer`.

Sanitizer runs set `ASAN_OPTIONS=halt_on_error=1`,
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, or
`TSAN_OPTIONS=halt_on_error=1`. ASan/UBSan and TSan are separate builds. Run the
FileSystem and Pak fixtures sequentially across build configurations because
some older tests use fixed temporary paths. No full engine executable or renderer
target is required for these checks.

## Release measurements

Five existing benchmark executables ran successfully: runtime memory, Resource,
FileSystem paths, Pak, and System. Together they contain 33 workloads, each run
for five repetitions with a minimum 0.2 seconds per repetition. New arena cases
measure the entire allocate/commit, payload write, clear and optional decommit
cycle; resource cases now fail explicitly if a measured operation fails.

Selected median CPU timings on Apple Silicon macOS:

| Workload | Median CPU time | Unit of work |
| --- | ---: | --- |
| Arena, 64 KiB, retain committed pages | 1.624 µs | One allocate/write/clear/reset cycle |
| Arena, 64 KiB, decommit on reset | 10.950 µs | One allocate/write/clear/reset cycle |
| Arena, 1 MiB, retain committed pages | 25.777 µs | One allocate/write/clear/reset cycle |
| Arena, 1 MiB, decommit on reset | 79.829 µs | One allocate/write/clear/reset cycle |
| Pool, 1,024 slots, ordered free | 5.388 ns | One allocation or free; batch divided by 2,048 |
| Pool, 16,384 slots, shuffled free | 10.261 ns | One allocation or free; batch divided by 32,768 |
| Resource lookup | 5.175 ns | Get one existing resource through a handle |
| Resource cached acquire/release | 138.359 ns | One pair; owner keeps the resource loaded |
| Resource load/unload | 153.236 ns | One pair with a trivial in-memory test loader |
| Pak create, four 4 KiB files | 975.993 µs | Build, flush and publish one archive |
| Pak open/close reader | 27.420 µs | One reader lifecycle |
| Pak read by path, 4 KiB | 2.096 µs | One read from an already-open archive |
| FileSystem normalize virtual path | 123.518 ns | One benchmark path |
| System event queue/poll | 8.970 ns | One queue/poll pair |

Resource numbers exclude real asset decoding and I/O. Archive reads benefit from
the warm OS cache. Arena throughput counts logical payload bytes per cycle, not
physical memory traffic. Background load ranged roughly 5.8–11.2 during this
repeat; affinity and CPU-frequency reporting were unavailable. These are
current-behavior baselines, not before/after speedup measurements or portable
budgets.

Raw results and runner output are saved as
`expanded-startup-<target>-release.json` and `.log` in the evidence directory.
To repeat an individual workload:

```sh
cmake --build out/build/nonrenderer-release --target cypher_memory_bench -j 2
out/build/nonrenderer-release/bin/cypher_memory_bench \
  --benchmark_filter='^BM_Arena_VirtualWriteClearReset' \
  --benchmark_min_time=0.2s --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_out=out/validation/non-renderer-2026-09-16/arena-repeat.json \
  --benchmark_out_format=json
```

The isolated Release tree uses `bench-release` with its own binary directory and
the already-installed preset dependencies. No renderer benchmark is selected.
