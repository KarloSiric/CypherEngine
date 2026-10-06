<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/build_guide.md
//  Purpose: Documents build guide.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Build Guide

## Prerequisites

- Git
- CMake 3.21 or newer
- Ninja
- a C++20 compiler

Large dependencies are acquired through a project-local, pinned vcpkg checkout.
Small source integrations are Git submodules. Qt, FMOD, and the Vulkan SDK remain
external and are not required by the current runtime build.

## First checkout

```bash
git submodule update --init --recursive
cmake -P cmake/CypherBootstrapVcpkg.cmake
```

The bootstrap revision comes from `vcpkg.json`. Sources and package builds remain
under the ignored `.deps/` and `out/` directories.

## Build

```bash
cmake --preset debug
cmake --build --preset debug
./out/build/debug/bin/CypherEngine
```

## C++ language server and Sublime Text

clangd needs the real CMake compile commands to resolve target-specific include
paths, definitions, Qt settings, and C++20 flags. The repository `.clangd` reads
`build-clangd/compile_commands.json`, an ignored link to the selected build.
After configuring a build, select it without compiling any C++ targets:

```bash
cmake --build out/build/debug --target cypher_clangd
```

The target requires Python 3.9 or newer and a Makefiles or Ninja generator.
The helper can also select a preset or any existing build directory directly:

```bash
python3 tools/dev/generate_clangd_compile_db.py --preset debug
python3 tools/dev/generate_clangd_compile_db.py --build-dir build-mason
```

For the current compact local Mason build, reuse its configured cache and
installed dependencies:

```bash
cmake -S . -B build-mason -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-mason --target cypher_clangd
```

These two commands regenerate build metadata and select the database; they do
not rebuild Mason. The vcpkg presets still require the pinned checkout described
above. Do not bootstrap packages or create another build tree just to repair
indexing in an already configured local build.

The link follows subsequent CMake database regeneration automatically. On
systems that disallow symlinks, the helper publishes an atomic copy instead;
rerun the target after reconfiguration to refresh that copy. Missing or invalid
input leaves the previous selected database untouched.

In Sublime Text, use LSP-clangd and let the repository `.clangd` choose the
database. Remove a stale project-specific `clangd.compile-commands-dir` override
if it points to a removed build. Then run **LSP: Restart Server** from the Command
Palette while a Cypher C++ file is active. A missing compile database can cause
one missing header to cascade into many unknown-type and namespace diagnostics;
adding arbitrary include paths to silence those errors is not a build repair.

## Planned convenience layer

A top-level `build.sh` will be introduced as a thin wrapper so the day-to-day build flow stays simple over the life of the project.

That script should remain:
- thin
- explicit
- a wrapper around the real build system

It should not replace the real build configuration.

## Long-term build picture

The intended full project has multiple build bodies:

- engine runtime
- standalone `rvm`
- game scripts
- tools

That means the eventual top-level build flow must account for:
- runtime compilation
- VM compilation
- script compilation
- asset pipeline invocation

## Current rule

Use the simplest build path that supports the current milestone.

## Tests and benchmarks

```bash
ctest --preset debug

cmake --preset bench-release
cmake --build --preset bench-release
```

The presets select only their required vcpkg feature groups. To resolve and
install every approved optional dependency for integration work:

```bash
cmake --preset dependencies-all
```

Declaring or installing a package does not make it a runtime dependency. The
owning subsystem must still provide a Cypher wrapper, tests, and an explicit CMake
link relationship.
