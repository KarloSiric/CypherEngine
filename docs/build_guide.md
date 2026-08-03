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
- CMake 3.20 or newer
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
