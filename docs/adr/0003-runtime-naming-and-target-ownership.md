<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/adr/0003-runtime-naming-and-target-ownership.md
//  Purpose: Defines runtime symbol naming and incremental CMake ownership.
//  Details: Keeps the C-style runtime API compact without weakening module
//           boundaries or forcing a repository-wide rename.
//
//  History:
//  - Created by Karlo Siric on 2026-08-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ADR 0003: Runtime Naming And Target Ownership

## Status

Accepted.

## Context

The runtime currently mixes long `CypherSubsystem_Function` names, nested
subsystem namespaces, broad include paths, and a recursive CMake source glob.
This makes ownership harder to see and allows tests to compile implementation
files directly instead of consuming the same library as the engine.

Cypher uses C++20, but its runtime style is deliberately C-oriented: explicit
state, structs, free functions, visible data flow, and short module prefixes.
The useful lesson from Quake and GoldSrc is local readability and clear module
ownership, not mechanical imitation of historical source code.

## Decision

Public runtime operations use a short owner prefix inside `cypher::engine`:

| Prefix | Owner |
| --- | --- |
| `Host_` | process lifecycle and frame orchestration |
| `Sys_` | operating-system and platform services |
| `Com_` | runtime-wide coordination that has no narrower owner |
| `FS_` | mounted filesystem and virtual-path access |
| `Pak_` | package archive reading, writing, and verification |
| `Mem_` | runtime allocators and memory policy |
| `Log_` | runtime logging sinks and formatted records |
| `Cmd_` | command registration and execution |
| `Cbuf_` | buffered command text and deferred execution |
| `Cvar_` | console-variable registry and values |
| `Cfg_` | configuration loading and application |
| `Res_` | runtime resource ownership and lookup |
| `R_` | renderer frontend and backend-neutral submission |
| `RB_` | renderer backend contract |
| `GL_` | private OpenGL backend operations |
| `IN_` | input collection and state |
| `NET_` | socket and transport services |
| `Netchan_` | packet sequencing and reliable-channel logic |
| `SV_` | authoritative server runtime |
| `CL_` | client runtime |
| `CM_` | collision/world query model |
| `G_` | server-side game code |
| `CG_` | client-side presentation code |
| `S_` | runtime sound system |

For example, the package API is `cypher::engine::Pak_OpenReader`, not
`cypher::engine::pak::CypherPak_OpenReader` and not
`cypher::engine::pak::Pak_OpenReader`. Private implementation helpers may use a
`detail` namespace or an unnamed namespace.

The short-prefix rule applies to runtime operations. Descriptive names remain
mandatory for:

- source files and directories
- CMake targets and aliases
- serialized format constants and magic values
- public SDK/ABI types whose identity must remain stable
- tool products, compiler modules, and executable names

Consequently, names such as `CypherPak_Reader.cpp`, `Cypher::Pak`, and
`CYPHER_PAK_FORMAT_VERSION` remain explicit. Type names are changed only when a
module is migrated and a clearer name is proven; a global type rename is not
part of this decision.

## Target Ownership

Every migrated runtime subsystem must have:

1. an explicit source list
2. one named static library target and `Cypher::` alias
3. only the include paths and libraries it actually consumes
4. tests and benchmarks that link the target instead of recompiling its `.cpp`
   files
5. removal of its implementation files from the monolithic runtime source set

Static libraries are the default during Cypher 1 development. DLL/shared-library
boundaries are introduced only for an actual reload, plugin, or deployment
requirement; function tables and visibility macros are not added speculatively.

## Dependency Direction

Dependencies move from high-level owners toward lower-level contracts:

```text
Host / game / tools
        |
        v
runtime subsystem targets
        |
        v
Common contracts and platform foundations
```

A subsystem must not depend on Host. Renderer backends must not reach into the
filesystem, package reader, or game code. Logging must not be required by the
lowest system layer during early process startup. Cross-subsystem calls use a
direct API while ownership is static and in-process; function tables are kept
for genuine backend, plugin, or DLL boundaries.

## Migration Rules

Migration is deliberately incremental:

1. establish a green build and test baseline
2. choose one coherent, low-coupling subsystem
3. rename its public operations and all in-repository callers atomically
4. create its explicit CMake target
5. run focused, complete Debug, and sanitizer verification
6. commit before selecting the next subsystem

No repository-wide search-and-replace is permitted. Faux-Hungarian local names
such as `sz`, `p`, and `n` are reconsidered only inside the subsystem currently
being migrated. Existing serialized layouts and behavior do not change during a
naming/ownership migration.

Host, renderer design, physics, ECS, AI, animation, networking, and gameplay
architecture remain separate collaborative design steps. This ADR does not
authorize their implementation.

## First Migration

`CypherPak` is the first runtime target migrated under this policy. It is a
contained package-format owner with focused tests and no dependency on Host, so
it can validate the naming and target process before filesystem, memory,
logging, command, CVar, configuration, or system modules are touched.

## Consequences

- Runtime call sites become compact without losing module identity.
- CMake expresses actual ownership instead of relying solely on directory layout.
- Tests exercise the same built library consumed by the engine.
- Refactoring remains reviewable and reversible one subsystem at a time.
- Future engine systems begin from a measurable dependency graph rather than a
  monolithic executable target.
