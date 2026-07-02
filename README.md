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

CypherEngine is a C++20 game engine project with C-style runtime code.

The engine is being built from scratch for learning and for eventually making a real 3D game. The codebase favors plain structs, free functions, explicit ownership, module prefixes, and data-oriented systems over class-heavy architecture.

Current work includes a common runtime layer, custom memory allocators, virtual filesystem, CypherPak package archives, SDL3 windowing, OpenGL/glad rendering bootstrap, math foundations, logging, config, commands, cvars, tests, and benchmarks.

CypherEngine studies ideas from idTech, GoldSrc/Source, and early CryEngine-era engines as engineering references. It is not a fork and does not copy their implementations.

## Stack

- C++20
- CMake
- vcpkg
- SDL3
- OpenGL with glad
- Catch2
- Google Benchmark
- FreeType, HarfBuzz, libpng, libjpeg-turbo
- OpenAL Soft
- libsodium
- LZ4, Zstd, xxHash
- meshoptimizer
- Tracy

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
./build-bench/bin/cypher_common_char_bench
./build-bench/bin/cypher_memory_bench
./build-bench/bin/cypher_filesystem_path_bench
./build-bench/bin/cypher_pak_bench
```

## Documentation

- [docs/index.md](docs/index.md)
- [docs/current_status.md](docs/current_status.md)
- [docs/architecture.md](docs/architecture.md)
- [docs/subsystems.md](docs/subsystems.md)
- [docs/coding_style.md](docs/coding_style.md)
- [docs/reference_engine_lessons.md](docs/reference_engine_lessons.md)

## License

CypherEngine is proprietary software owned by Karlo Siric. See [LICENSE](LICENSE).
