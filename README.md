<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: README.md
//  Purpose: Introduces the CypherEngine repository.
//  Details: This top-level page stays brief and points readers to build, test,
//           benchmark, and documentation entry points. It should describe the current
//           project state without promising unfinished systems.
//
//  History:
//  - Created by Karlo Siric on 2026-04-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine

CypherEngine is an early-stage, from-scratch C++20 3D game engine and offline
asset toolchain. Runtime code favors C-style procedural APIs, explicit ownership,
module prefixes, and data-oriented systems.

The project studies id Tech, GoldSrc/Source, and early CryEngine architecture as
engineering references. It is not a fork of those engines and does not copy their
implementations.

> **Project status:** Active pre-1.0 development. Public APIs, runtime behavior,
> and authored/cooked resource formats may change while the foundation is being
> established. The engine is not production-ready.

## Current Milestone

Current work is making runtime ownership visible before the first World,
Entity, Physics, Audio, Font, and UI vertical slices:

- `CypherEngine` is a thin executable and `CypherHost` is its composition root
- `CypherSystem` is the single operating-system and platform boundary
- Log, Memory, FileSystem, Command, CVar, Config, and Host have explicit CMake
  targets instead of being collected by an executable-wide source glob
- `CypherWorld` remains the renderer-neutral spatial-world owner
- `CypherMath` has a documented coordinate/numerical contract and a verified
  test, sanitizer, and benchmark baseline
- `CypherFont` and `CypherUI` have accepted ownership boundaries and must begin
  with complete runtime text and interface slices

## Engineering Principles

- Build the playable runtime before broad editor expansion.
- Keep ownership and lifetime rules explicit.
- Prefer structs and free functions over inheritance-heavy designs.
- Keep platform-native headers behind narrow subsystem boundaries.
- Add abstractions only when a concrete runtime or tool consumer requires them.
- Treat reference engines as sources of lessons, not source-code templates.

## Repository Layout

| Path | Responsibility |
| --- | --- |
| `src/CypherCommon` | Shared types, primitives, math, formats, and neutral contracts |
| `src/CypherSystem` | Engine-facing operating-system services and target backends |
| `src/CypherLog` | Structured logging, filtering, formatting, and output sinks |
| `src/CypherMemory` | Runtime arenas, pools, scratch storage, and memory diagnostics |
| `src/CypherFileSystem` / `src/CypherPak` | Runtime mounts, file access, watches, and package archives |
| `src/CypherEngine/CypherHost` | High-level runtime startup, frame, and shutdown orchestration |
| `src/CypherResource` | Runtime resource identity, loading, caching, and ownership |
| `src/CypherRender` | Renderer-facing runtime implementation |
| `src/CypherWorld` | Planned world ownership, spatial queries, visibility, environment, and streaming |
| `src/CypherFont` / `src/CypherUI` | Planned renderer-neutral text and runtime interface systems |
| `src/CypherTools` | Offline compiler and future authoring-tool products |
| `tests` / `benchmarks` | Correctness, regression, and performance coverage |
| `docs` | Architecture decisions, current status, formats, and development notes |

## Requirements

- CMake 3.21 or newer
- A C++20 compiler
- Git with submodule support
- Platform SDK and build tools for Windows, macOS, or Linux

Dependencies are pinned through `vcpkg.json` and the approved vendored libraries
under `thirdparty/`.

## Build

```bash
git submodule update --init --recursive
cmake -P cmake/CypherBootstrapVcpkg.cmake
cmake --preset debug
cmake --build --preset debug --parallel
./out/build/debug/bin/CypherEngine
```

## Tests

```bash
ctest --preset debug --output-on-failure
```

The sanitizer preset provides a second validation path on supported toolchains:

```bash
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan --parallel
ctest --preset asan-ubsan --output-on-failure
```

## Benchmarks

```bash
cmake --preset bench-release
cmake --build --preset bench-release
./out/build/bench-release/bin/cypher_common_string_bench
```

## Documentation

- [docs/index.md](docs/index.md)
- [docs/current_status.md](docs/current_status.md)
- [docs/architecture.md](docs/architecture.md)
- [docs/cyphercommon_architecture.md](docs/cyphercommon_architecture.md)
- [docs/subsystems.md](docs/subsystems.md)
- [docs/cryengine1_subsystem_research.md](docs/cryengine1_subsystem_research.md)
- [docs/mathlib_runtime_readiness.md](docs/mathlib_runtime_readiness.md)
- [docs/adr/0006-runtime-subsystem-structure.md](docs/adr/0006-runtime-subsystem-structure.md)
- [docs/coding_style.md](docs/coding_style.md)
- [docs/reference_engine_lessons.md](docs/reference_engine_lessons.md)
- [docs/TILEEDITOR_DEVELOPMENT_KIT.md](docs/TILEEDITOR_DEVELOPMENT_KIT.md)
- [CHANGELOG.md](CHANGELOG.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)

## License

CypherEngine is proprietary software owned by Karlo Siric. See [LICENSE](LICENSE).
