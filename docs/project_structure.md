<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/project_structure.md
//  Purpose: Documents project structure.
//  Details: This documentation records architecture, policy, or planning decisions
//           for future engine work. It should explain intent and tradeoffs rather
//           than duplicate source code.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Project Structure

This is the intended long-term CypherEngine repository structure.

`CypherEngine` is the engine runtime. The game name can stay separate once the actual game identity is locked.

## Target layout

```text
CypherEngine/
├── README.md
├── CMakeLists.txt
├── vcpkg.json
├── src/
│   ├── CypherCommon/
│   │   └── Engine/
│   ├── CypherEngine/
│   │   └── CypherHost/
│   ├── CypherAI/
│   ├── CypherAnimation/
│   ├── CypherAudio/
│   ├── CypherCommand/
│   ├── CypherConfig/
│   ├── CypherConsole/
│   ├── CypherCVar/
│   ├── CypherEntity/
│   ├── CypherFileSystem/
│   ├── CypherInput/
│   ├── CypherLog/
│   ├── CypherMemory/
│   ├── CypherNetwork/
│   ├── CypherPak/
│   ├── CypherPhysics/
│   ├── CypherPlatform/
│   ├── CypherProfile/
│   ├── CypherRender/
│   ├── CypherResource/
│   ├── CypherScript/
│   ├── CypherSystem/
│   ├── CypherWorld/
│   ├── CypherEditor/
│   ├── CypherTools/
│   ├── CypherGame/
│   ├── CypherClient/
│   ├── CypherServer/
├── rvm/
├── game/
├── tools/
│   ├── CypherAssetCompiler/
│   ├── CypherMapCompiler/
│   └── CypherResourceCompiler/
├── data/
├── config/
├── thirdparty/
└── docs/
```

## Meaning of each top-level directory

- `src/CypherEngine`
  - thin executable host, startup composition, frame ownership, and shutdown ordering
- `src/CypherCommon`
  - shared public/common foundation, custom runtime utilities, primitive types, handles, format headers, and public subsystem contracts
- `src/CypherSystem`
  - central engine lifetime orchestration; long-term replacement for host-style bootstrapping
- `src/CypherPlatform`
  - OS/window/platform backends; current platform code can migrate here later
- `src/CypherMemory`
  - arenas, pools, memory stats, diagnostics, and allocator backends
- `src/CypherFileSystem`
  - mounted paths, virtual paths, file handles, archive/package access
- `src/CypherConsole`
  - developer console front-end over commands and CVars
- `src/CypherCommand`
  - command registration and execution
- `src/CypherCVar`
  - runtime variables, flags, and tweakable settings
- `src/CypherConfig`
  - cfg file loading and command-line config execution
- `src/CypherResource`
  - asset handles, loading, dependencies, reload, and lifecycle tracking
- `src/CypherRender`
  - renderer front-end, cameras, draw lists, shaders, meshes, materials, backend dispatch
- `src/CypherWorld`
  - map/world source data, object placement, scene ownership, level metadata
- `src/CypherEntity`
  - entity identity, component ownership, and game-object runtime bridge
- `src/CypherInput`
  - keyboard, mouse, controller, input contexts, editor/game routing
- `src/CypherPhysics`
  - collision, traces, physics bodies, simulation, and movement helpers
- `src/CypherAudio`
  - audio devices, mixers, sound resources, playback, and spatial audio
- `src/CypherAI`
  - navigation, perception, behavior, and combat decision systems
- `src/CypherAnimation`
  - skeletons, clips, blending, animation graphs, and pose evaluation
- `src/CypherNetwork`
  - sockets, packets, channels, replication, prediction, and sessions
- `src/CypherScript`
  - VM/native bridge and gameplay scripting integration
- `src/CypherProfile`
  - profiling scopes, counters, telemetry, and memory/performance reporting
- `src/CypherEditor`
  - Qt editor application, viewports, inspectors, asset browser, world editing tools
- `src/CypherTools`
  - shared native code for command-line tools and offline processors
- `src/CypherGame`
  - native game/runtime bridge code when needed
- `src/CypherClient`
  - local player, prediction, presentation, HUD, input bridge
- `src/CypherServer`
  - authoritative simulation and multiplayer/session ownership
- `rvm`
  - standalone Cypher VM project
- `game`
  - gameplay scripts intended to run on the VM
- `tools`
  - standalone asset and pipeline tools, resource compilers, map compilers, importers
- `data`
  - runtime assets
- `config`
  - default cfg files
- `thirdparty`
  - vendored external libraries when vcpkg is not the better fit
- `docs`
  - architecture and process documentation

## Target `src/CypherCommon` layout

`CypherCommon` should grow as the public/common layer, not as a dump for full
subsystem implementations.

Target shape:

```text
src/CypherCommon/
├── CypherCommon.h
├── CypherCommon_Config.h
├── CypherCommon_Version.h
├── CypherCommon_Public.h
├── Tier0/
├── Tier1/
├── Tier2/
├── Tier3/
├── Core/
├── Sys/
├── Utl/
├── Memory/
├── Text/
├── Color/
├── MathBase/
├── Image/
├── IO/
├── Hash/
├── Parse/
├── Serialization/
├── Reflection/
├── Formats/
├── Job/
├── Asset/
├── Resource/
├── Scene/
├── World/
├── Entity/
├── Animation/
├── AI/
├── Script/
├── Engine/
├── FileSystem/
├── Renderer/
├── Material/
├── Texture/
├── Input/
├── Audio/
├── Physics/
├── Network/
├── Gui/
├── Tools/
└── Editor/
```

These folders should be created as real files arrive. Empty folder trees should
not be used as a substitute for working code or clear documentation.

## Important note about the current repo

Runtime subsystems are top-level siblings under `src/`. This follows the useful
part of the CryEngine 1 layout: ownership boundaries are visible in the source
tree instead of being hidden inside an umbrella executable directory.

`src/CypherEngine/` is intentionally narrow. It contains the host/composition
layer, not renderer, resource, filesystem, memory, audio, physics, or other
reusable subsystem implementations. Shared runtime metadata, error encoding,
and bootstrap print contracts live under `src/CypherCommon/Engine/`.

The newer common foundation lives under `src/CypherCommon/` with Tier0/Tier1
work already in progress. New common/public contracts should prefer that tree.

New `Cypher*` runtime modules belong beside the existing top-level subsystem
directories. Tests and benchmarks mirror those subsystem names outside `src/`.

A top-level folder is not automatically a DLL. Each subsystem first needs a
clean public contract, an owning CMake target, explicit dependencies, and tests.
Most internal modules should begin as static libraries. Shared-library or
function-table boundaries should be introduced only where runtime replacement,
plugins, process separation, or a stable ABI requires them.

`CypherSystem` currently contains some platform/window code. Long term,
platform-specific code should migrate into `CypherPlatform`, leaving
`CypherSystem` free to become the central engine orchestration layer.

The former `src/CypherEngine/CypherMath` tree was an unbuilt legacy duplicate.
`src/CypherCommon/Mathlib` and the `Cypher::Math` target are the canonical math
implementation shared by the runtime and tools.

Empty future folders may exist before implementation. They are architectural
parking spaces, not a promise that those systems are complete.

## Key architectural message

This project is not just one executable.

It is a family of connected systems:
- native engine runtime
- scripting VM
- gameplay layer
- tools pipeline
- asset/data tree

That is why the structure must be explicit.
