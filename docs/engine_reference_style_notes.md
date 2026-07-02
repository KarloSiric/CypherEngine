<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/engine_reference_style_notes.md
//  Purpose: Documents engine reference style notes.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-06-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Engine Reference Style Notes

These notes summarize style lessons from public reference engines. They are for
architecture and naming guidance only. Do not copy implementation code.

## Source SDK 2013

Source SDK separates shared code into tiers:

- `tier0`: base types, platform macros, asserts, debug output, memory hooks,
  timers, atomics/thread helpers and low-level profiler support.
- `tier1`: string tools, byte swapping, checksums, buffers, bit streams,
  command/CVar-style utilities, memory pools/stacks and utility containers.
- `tier2`: higher-level file, render, mesh, sound and tool helpers.
- `tier3`: model, scene and editor-adjacent helpers.

Useful naming patterns:

- `pName` for pointers.
- `ppName` for pointer-to-pointer values.
- `pszName` for zero-terminated strings.
- `szName` for fixed string buffers.
- `nName` for sizes, counts, byte counts and capacities.
- `iName` for indexes.
- `bName` for booleans.
- `flName` for floats.
- `hName` for handles.
- `pfnName` for function pointers.
- `m_Name`, `g_Name`, `s_Name` for member, global and static storage.

The key lesson is density with intent: engine code can stay C-like and still be
clear if names encode ownership, storage and data shape.

## idTech 3

Quake III centralizes shared definitions in `q_shared.h` and keeps common
runtime systems under `qcommon`. It favors fixed limits, simple structs,
module-prefixed free functions, direct data flow and explicit platform sections.

Useful lessons for Cypher:

- Keep one low-level common layer included by almost everything.
- Use fixed-size names and limits deliberately.
- Prefer simple handles and indexes over hidden ownership.
- Keep renderer, filesystem, networking and VM-facing contracts explicit.

## Cypher Direction

Cypher will use a root `CypherCommon` layer outside `CypherEngine`, then
engine/game/tools/editor layers above it. The migration order is:

1. `CypherCommon/Tier0`: base types, compiler/platform macros, asserts,
   alignment, atomics and build configuration.
2. `CypherCommon/Tier1`: string helpers, byte order, hashing, checksums,
   buffers, command parsing and small utility containers.
3. Runtime subsystems: memory, filesystem, package archives, console/CVars,
   platform, renderer, resources and networking.
4. Tools/editor support after the runtime contracts are stable.

## Shipped Engine Architecture Lessons

Classic shipped engine trees show that a complete engine is a runtime plus a
toolchain. The useful lessons are:

- common/interface boundaries matter more than file count
- the host/system layer should own explicit boot, update and shutdown order
- VFS, package archives, streaming and resources are separate layers
- resource compilers exist to move expensive transformation out of runtime
- renderer resources should become handle-owned engine resources
- memory diagnostics, cvars and profiler scopes are core engine features
- world/entity data must exist before a serious editor can edit anything real

See [reference_engine_lessons.md](reference_engine_lessons.md) for the full
policy and milestone translation.
