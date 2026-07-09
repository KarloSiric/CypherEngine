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

CypherEngine is a C++20 game engine project written with C-style runtime code, explicit ownership, module prefixes, and data-oriented systems.

CypherEngine studies ideas from idTech, GoldSrc/Source, and early CryEngine-era engines as engineering references. It is not a fork and does not copy their implementations.

Current work is focused on the common runtime foundation: Tier0/Tier1 utilities, memory, VFS, package archives, diagnostics, tests, and benchmarks.

## Stack

- C++20
- CMake
- vcpkg
- Catch2
- Google Benchmark
- SDL3
- OpenGL with glad
- OpenAL Soft
- LZ4, Zstd, xxHash, libsodium
- meshoptimizer, FreeType, HarfBuzz
- Tracy later for profiling

## Build

```bash
cmake -S . -B build
cmake --build build
./build/bin/CypherEngine
```

## Tests

```bash
cmake -S . -B build -DCYPHERENGINE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error
```

## Benchmarks

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DCYPHERENGINE_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/bin/cypher_common_string_bench
```

## Documentation

- [docs/index.md](docs/index.md)
- [docs/current_status.md](docs/current_status.md)
- [docs/architecture.md](docs/architecture.md)
- [docs/cyphercommon_architecture.md](docs/cyphercommon_architecture.md)
- [docs/subsystems.md](docs/subsystems.md)
- [docs/coding_style.md](docs/coding_style.md)
- [docs/reference_engine_lessons.md](docs/reference_engine_lessons.md)

## License

CypherEngine is proprietary software owned by Karlo Siric. See [LICENSE](LICENSE).
