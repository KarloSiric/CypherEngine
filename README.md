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
- Lua for scripting
- Tracy for profiling
- Qt 6 later for Mason

## Build

```bash
git submodule update --init --recursive
cmake -P cmake/CypherBootstrapVcpkg.cmake
cmake --preset debug
cmake --build --preset debug
./out/build/debug/bin/CypherEngine
```

## Tests

```bash
ctest --preset debug
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
- [docs/coding_style.md](docs/coding_style.md)
- [docs/reference_engine_lessons.md](docs/reference_engine_lessons.md)

## License

CypherEngine is proprietary software owned by Karlo Siric. See [LICENSE](LICENSE).
