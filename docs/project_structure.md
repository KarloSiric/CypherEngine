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
│   ├── CypherEngine/
│   │   ├── CypherAI/
│   │   ├── CypherAnimation/
│   │   ├── CypherAudio/
│   │   ├── CypherConsole/
│   │   ├── CypherSystem/
│   │   ├── CypherPlatform/
│   │   ├── CypherFileSystem/
│   │   ├── CypherResource/
│   │   ├── CypherCommand/
│   │   ├── CypherCVar/
│   │   ├── CypherConfig/
│   │   ├── CypherHost/
│   │   ├── CypherInput/
│   │   ├── CypherMath/
│   │   ├── CypherMemory/
│   │   ├── CypherNetwork/
│   │   ├── CypherPhysics/
│   │   ├── CypherProfile/
│   │   ├── CypherRender/
│   │   ├── CypherScript/
│   │   ├── CypherEntity/
│   │   └── CypherWorld/
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
  - native CypherEngine runtime
- `src/CypherCommon`
  - shared public/common foundation, custom runtime utilities, primitive types, handles, format headers, and public subsystem contracts
- `src/CypherEngine/CypherSystem`
  - central engine lifetime orchestration; long-term replacement for host-style bootstrapping
- `src/CypherEngine/CypherPlatform`
  - OS/window/platform backends; current platform code can migrate here later
- `src/CypherEngine/CypherMemory`
  - arenas, pools, memory stats, diagnostics, and allocator backends
- `src/CypherEngine/CypherFileSystem`
  - mounted paths, virtual paths, file handles, archive/package access
- `src/CypherEngine/CypherConsole`
  - developer console front-end over commands and CVars
- `src/CypherEngine/CypherCommand`
  - command registration and execution
- `src/CypherEngine/CypherCVar`
  - runtime variables, flags, and tweakable settings
- `src/CypherEngine/CypherConfig`
  - cfg file loading and command-line config execution
- `src/CypherEngine/CypherResource`
  - asset handles, loading, dependencies, reload, and lifecycle tracking
- `src/CypherEngine/CypherRender`
  - renderer front-end, cameras, draw lists, shaders, meshes, materials, backend dispatch
- `src/CypherEngine/CypherWorld`
  - map/world source data, object placement, scene ownership, level metadata
- `src/CypherEngine/CypherEntity`
  - entity identity, component ownership, and game-object runtime bridge
- `src/CypherEngine/CypherInput`
  - keyboard, mouse, controller, input contexts, editor/game routing
- `src/CypherEngine/CypherPhysics`
  - collision, traces, physics bodies, simulation, and movement helpers
- `src/CypherEngine/CypherAudio`
  - audio devices, mixers, sound resources, playback, and spatial audio
- `src/CypherEngine/CypherAI`
  - navigation, perception, behavior, and combat decision systems
- `src/CypherEngine/CypherAnimation`
  - skeletons, clips, blending, animation graphs, and pose evaluation
- `src/CypherEngine/CypherNetwork`
  - sockets, packets, channels, replication, prediction, and sessions
- `src/CypherEngine/CypherScript`
  - VM/native bridge and gameplay scripting integration
- `src/CypherEngine/CypherProfile`
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

The current source already lives under `src/CypherEngine/`, with CryEngine-style subsystem folders such as `CypherRender`, `CypherSystem`, and `CypherFileSystem`.

The newer common foundation lives under `src/CypherCommon/` with Tier0/Tier1
work already in progress. New common/public contracts should prefer that tree.

Future migrations should add new `Cypher*` modules beside the existing runtime modules instead of inventing a different architecture each time.

`CypherSystem` currently contains some platform/window code. Long term,
platform-specific code should migrate into `CypherPlatform`, leaving
`CypherSystem` free to become the central engine orchestration layer.

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
