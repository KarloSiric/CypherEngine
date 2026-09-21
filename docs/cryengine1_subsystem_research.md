<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/cryengine1_subsystem_research.md
//  Purpose: Records a provenance-aware CryEngine 1 subsystem architecture audit.
//  Details: Maps historical Far Cry engine responsibilities to original Cypher
//           boundaries without importing or adapting unlicensed implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CryEngine 1 Subsystem Research And Cypher Mapping

## Status And Decision Summary

This is a structural research document. It records what the Far Cry-era
CryEngine 1 modules owned, how the process composed them, where apparently
"missing" features actually lived, and which boundaries help CypherEngine.

It does not authorize use of CryEngine source code. Cypher implementations must
be original and derived from Cypher requirements, public behavior, published
documentation, mathematics, and independently designed tests.

The resulting Cypher decisions are:

1. Keep `CypherWorld`. Do not rename it to `Cypher3DEngine`.
2. Keep `CypherEngine` as the product/executable target and keep its entry point
   thin.
3. Keep `CypherHost` as the composition and lifecycle owner.
4. Keep `CypherSystem` as the single operating-system/platform boundary. Do not
   create a competing `CypherPlatform` runtime target.
5. Add focused `CypherFont` and `CypherUI` boundaries, beginning with complete
   vertical slices rather than empty APIs.
6. Put collision, rigid bodies, character movement, and projectile sweeps in
   `CypherPhysics`.
7. Put entity identity and component lifetime in `CypherEntity`; represent
   triggers, lights, audio emitters, and effect placements as components whose
   behavior is executed by their owning systems.
8. Put world representation, terrain, environment volumes, spatial indexes,
   portals/PVS data, visibility extraction, light placement, and streaming
   policy in `CypherWorld`.
9. Keep simple particle placement/simulation with World-facing effect records
   and renderer execution until an effect asset, independent simulation
   lifetime, extraction API, tests, and multiple consumers justify `CypherVFX`.
10. Do not use line count or folder count as a definition of subsystem quality.
    A small module with one coherent lifetime and a tested end-to-end path is a
    complete architectural boundary.

These decisions are formalized in
[ADR 0006](adr/0006-runtime-subsystem-structure.md).

## Provenance And Research Boundary

The official Far Cry Mod SDK and public full-engine mirrors are different
artifacts.

The archived Far Cry SDK 1.4 readme says the public package released the C++
source for `CryGame.dll` along with documentation, samples, exporters, plug-ins,
source assets, and selected format code. It does not describe a public release
of the complete engine, editor, renderer backends, middleware, or online-service
source. See the preserved
[Far Cry SDK 1.4 readme](https://www.gamepressure.com/download/far-cry-sdk-v14-mod/zf35eb)
and Crytek's later distinction between game code and engine code in
[Getting Started with Game Code](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/23306497).

The primary historical description used here is Crytek's 2005 *Far Cry Engine
Overview*, Revision 6, distributed with the Mod SDK. An indexed URL remains at
[Far Cry Engine Overview.pdf](https://fcdb.crymods.net/resources/pdf/sdk/Far%20Cry%20Engine%20Overview.pdf),
although the current host redirects human downloads to a suspended-account
page. The module descriptions below are based on the indexed document and are
cross-checked against current official CryEngine interface documentation.

Two GitHub trees were inspected only for structure:

- [`OpenFarCry1/Far-Cry-1-CryEngine1` at
  `38da5286bc420aa716662763cbde398473abe893`](https://github.com/OpenFarCry1/Far-Cry-1-CryEngine1/tree/38da5286bc420aa716662763cbde398473abe893)
  has no root license or provenance document and GitHub detects no license.
- [`StrongPC123/Far-Cry-1-Source-Full` at
  `1a41d18ccd9b6a7071a72291d366ec85e66c48c7`](https://github.com/StrongPC123/Far-Cry-1-Source-Full/tree/1a41d18ccd9b6a7071a72291d366ec85e66c48c7)
  describes itself as leaked and also has no detected license.

Those mirrors are unsuitable implementation sources. This audit used them only
to observe directory names, Visual Studio output types, public interface
locations, exported factory names, and coarse dependency/build relationships.
No function body, algorithm implementation, source comment, constant table, or
data layout from those trees may be copied, translated, or mechanically adapted
into Cypher.

The OpenFarCry tree also contains later community changes, and the two mirrors
do not contain identical material. They are evidence of a directory/build
shape, not pristine or authoritative release archives.

## Audit Method And Scale

The pinned OpenFarCry tree contains 4,131 files. For a reproducible coarse
inventory, files ending in `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hh`, `.hpp`,
`.hxx`, `.inl`, `.asm`, or `.s` were counted as code. Physical line counts
include comments and blank lines.

| Classification | All files | Code files | Physical code lines |
| --- | ---: | ---: | ---: |
| complete pinned tree | 4,131 | 3,394 | 1,162,446 |
| explicit bundled vendor/SDK subtrees | 1,666 | 1,266 | 350,096 |
| engine, game, tool, or unclassified remainder | 2,465 | 2,128 | 812,350 |

The remainder is a path classification, not a legal authorship determination.
The repository provides no reliable attribution manifest. Explicitly excluded
vendor paths include STLport, FreeType, Lua, several Expat and zlib copies,
Ogg/Vorbis headers, DirectX SDK material, Cg integrations, NVParse, NvTriStrip,
PunkBuster SDK material, and curl.

Representative module counts demonstrate why raw size is misleading:

| Module | Repository code files / lines | After explicit vendor exclusion |
| --- | ---: | ---: |
| `Cry3DEngine` | 112 / 44,361 | 112 / 44,361 |
| `CryAISystem` | 41 / 21,995 | 41 / 21,995 |
| `CryAnimation` | 155 / 35,455 | 155 / 35,455 |
| `CryCommon` | 128 / 51,365 | 128 / 51,365 |
| `CryEntitySystem` | 20 / 11,608 | 20 / 11,608 |
| `CryFont` | 334 / 134,050 | **20 / 4,946** |
| `CryGame` | 179 / 93,181 | 179 / 93,181 |
| `CryInput` | 20 / 9,349 | 20 / 9,349 |
| `CryMovie` | 65 / 19,001 | 48 / 7,984 |
| `CryNetwork` | 54 / 15,853 | 54 / 15,853 |
| `CryPhysics` | 72 / 31,599 | 72 / 31,599 |
| `CryScriptSystem` | 66 / 17,458 | **17 / 4,954** |
| `CrySoundSystem` | 34 / 10,101 | 29 / 9,374 |
| `CrySystem` | 174 / 63,817 | 132 / 44,823 |
| `Editor` | 752 / 213,143 | 710 / 194,131 |
| `RenderDll` | 345 / 255,853 | 244 / 198,421 |
| `ResourceCompiler` | 48 / 9,509 | 40 / 6,713 |
| `ResourceCompilerImage` | 17 / 3,589 | 17 / 3,589 |
| `ResourceCompilerPC` | 85 / 22,074 | 79 / 19,511 |
| `FARCRY` launcher | 2 / 918 | 2 / 918 |
| `FarCry_WinSV` | 9 / 2,220 | 9 / 2,220 |

FreeType accounts for 314 of the 334 code files and roughly 129,104 of the
134,050 physical lines under `CryFont`. The engine-owned font wrapper is about
20 code files. Embedded Lua similarly accounts for most of `CryScriptSystem`.
This is direct evidence that a focused `CypherFont` can remain small without
being architecturally deficient.

## Top-Level CryEngine 1 Products And Libraries

### Runtime libraries

- `CryCommon`
- `CrySystem`
- `Cry3DEngine`
- `CryAISystem`
- `CryAnimation`
- `CryEntitySystem`
- `CryFont`
- `CryInput`
- `CryMovie`
- `CryNetwork`
- `CryPhysics`
- `CryScriptSystem`
- `CrySoundSystem`

### Game and hosts

- `CryGame`
- `FARCRY`
- `FarCry_WinSV`

### Renderer implementations

- `RenderDll/Common`
- `RenderDll/XRenderD3D8`
- `RenderDll/XRenderD3D9`
- `RenderDll/XRenderOGL`
- `RenderDll/XRenderNULL`
- a partial `XRenderPS2` directory

The important non-renderer lesson is the high-level boundary: `Cry3DEngine`
organized scene work while an XRender implementation owned low-level graphics
resources and draw execution. A null renderer allowed non-visual process
profiles to retain the simulation runtime.

### Editor and offline tools

- `Editor`
- `ResourceCompiler`
- `ResourceCompilerPC`
- `ResourceCompilerImage`

### Historical platform/build overlays

- `Game01.sln`
- `Game01_XBox.sln`
- `AMD64_ProjectFiles_VS2005`
- Xbox-specific project files beside multiple modules
- `Win32APIWrapper`

The duplicated platform project definitions are build-system history rather
than a boundary Cypher should reproduce.

## Binary And Project Boundaries

The pinned [`Game01.sln`](https://github.com/OpenFarCry1/Far-Cry-1-CryEngine1/blob/38da5286bc420aa716662763cbde398473abe893/Game01.sln)
contains 22 project blocks. On the audited PC configurations, the following
were DLL products:

- the 13 major engine/game modules from `Cry3DEngine` through `CrySystem`;
- the D3D8, D3D9, OpenGL, and null renderers;
- `ResourceCompilerPC` and `ResourceCompilerImage`.

`CryCommon` was a utility/header project rather than a runtime binary. The
executables were `FarCry.exe`, `Editor.exe`, `FarCry_WinSV.exe`, and `rc.exe`.
Most Xbox configurations packaged corresponding engine code as static
libraries.

Many runtime modules did not express their true engine dependencies as ordinary
link dependencies. The launcher and `CrySystem` loaded DLLs and resolved
factory symbols at runtime. Factories returned C++ virtual interfaces declared
in `CryCommon`; therefore, the C-named export did not create a stable C ABI.
Interface layout still depended on compiler, build, headers, and C++ ABI.

Cypher should use static library targets by default. A dynamic boundary is
justified only by an actual hot-reload, third-party plug-in, process-isolation,
or distribution need. Stable external plug-ins require explicit ABI versions,
fixed-width data, ownership rules, and function tables or another deliberately
versioned boundary.

## Startup, Profiles, And Lifecycle

The historical composition path was:

```text
FarCry executable
        |
        | loads CrySystem and resolves its creation entry
        v
CrySystem composition/runtime owner
        |
        | loads implementation libraries and resolves factories
        v
interfaces declared in CryCommon
        |
        v
CryGame loaded after the core runtime exists
```

Observed factory names included:

| Implementation | Factory name |
| --- | --- |
| system | `CreateSystemInterface` |
| renderer backend | `PackageRenderConstructor` |
| script | `CreateScriptSystem` |
| network | `CreateNetwork` |
| physics | `CreatePhysicalWorld` |
| movie | `CreateMovieSystem` |
| input | `CreateInput` |
| sound | `CreateSoundSystem` |
| font | `CreateCryFontInterface` |
| AI | `CreateAISystem` |
| entity | `CreateEntitySystem` |
| animation | `CreateCharManager` |
| 3D/world | `CreateCry3DEngine` |
| game | `CreateGameInstance` |

The normal initialization order was approximately:

1. parse early command-line and process-profile state;
2. establish validation, logging, profiling, console, and CPU information;
3. initialize package/filesystem and mod mounts;
4. initialize central streaming;
5. initialize scripting;
6. register system variables, load configuration, and mount base packages;
7. initialize network;
8. initialize physics;
9. initialize movie/cinematic support;
10. initialize the selected renderer;
11. finalize console and timer;
12. initialize input;
13. initialize sound;
14. initialize fonts;
15. initialize AI;
16. finalize console registration;
17. initialize entities;
18. initialize animation;
19. initialize the 3D/world system;
20. install script bindings and download services;
21. return to the host and load/initialize the game module.

The profiles are more valuable than the exact order:

- dedicated server selected a null renderer and skipped input, sound, and font;
- editor mode delayed renderer, animation, and 3D-world creation until its
  embedded game/runtime view needed them;
- preview mode omitted network, input, sound, AI, and entities;
- Linux dedicated-server paths loaded platform-appropriate modules and retained
  the null renderer.

Cypher adopts explicit profiles and ordered rollback, but expresses them with
normal target dependencies and owned state:

```text
CypherEngine executable (main.cpp)
        |
        v
CypherHost composition and lifecycle
        |
        +-- System / Log / Memory
        +-- FileSystem / Pak / Resource
        +-- Command / CVar / Config
        +-- Input / Entity / Physics / Audio / Font
        +-- World / UI / Game
        `-- selected Render execution path
```

`CypherEngine` is the product name and executable target. `CypherHost` owns
construction, startup order, rollback, frame sequencing, shutdown, and future
client/server/editor/test profiles. `CypherSystem` provides OS-facing services.
This division fits the code that exists today and avoids introducing a second
composition layer simply to resemble CryEngine terminology.

## Subsystem Inventory

### CryCommon

Historical responsibility:

- shared public engine interface headers;
- vectors, matrices, quaternions, camera math, geometry helpers;
- platform/compiler definitions and scalar types;
- strings, smart pointers, streams, serialization, memory sizing;
- shared render and asset structures.

Every major library compiled against it, so it was both an interface hub and a
shared implementation bucket. Current official CryEngine documentation retains
the same broad idea: engine public APIs are exposed through a common header
root and global environment interfaces. See
[Introduction to the Engine API](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/26874885).

Cypher decision:

- keep domain-neutral primitives, math, formats, bounded containers, and truly
  shared contracts in Common;
- keep domain APIs beside their owning module;
- do not make Common a global service locator;
- remove duplicate foundations instead of letting legacy and canonical versions
  expand in parallel.

### CrySystem

Historical responsibility:

- composition and module loading;
- log, validation, console variables/commands, timer, profiling;
- Pak filesystem, mod mounts, and asynchronous streaming;
- XML, compressed files, memory reporting, CPU/platform detection;
- Lua debugger/script bindings, HTTP/download, and assorted security support.

This worked as the first runtime owner but accumulated many unrelated services.

Cypher decision:

- `CypherHost` keeps composition/lifecycle;
- `CypherSystem` keeps the OS/platform surface;
- Log, Memory, FileSystem, Pak, Command, CVar, Config, Resource, and future Jobs
  retain separate tested owners;
- no global interface exposing every subsystem.

### Cry3DEngine

Historical responsibility:

- height-field terrain, terrain sectors, texture pools, beaches, and water;
- static object and brush loading;
- outdoor/indoor spatial representation;
- visibility areas and portals;
- vegetation and sprite management;
- lights and lightmaps;
- decals and shadows;
- particle effects and emitters;
- materials;
- world-object streaming;
- high-level scene rendering and physicalization.

Cypher decision:

- map world lifetime, static placement, terrain, environment, spatial indexes,
  portals, PVS/visibility, world streaming, and renderer-neutral extraction to
  `CypherWorld`;
- leave native GPU execution in `CypherRender`;
- leave collision/physicalization in `CypherPhysics`;
- leave skeletal pose evaluation in `CypherAnimation`;
- keep effect placement and simple simulation close to World until a real VFX
  runtime earns an independent target.

`Cypher3DEngine` would reproduce an old umbrella name while hiding these clearer
owners, so `CypherWorld` remains the accepted name.

### CryEntitySystem

Historical responsibility:

- entity IDs, creation, removal, iteration, timers, and persistence;
- transforms and parenting;
- slots for static models and characters;
- direct representation of rendering, lights, particles, cameras, sound, AI,
  script, and physics;
- areas, relationships, lip sync, and facial-expression support.

This produced a broad entity object coupled to nearly every engine service.

Cypher decision:

- generation-checked entity identities;
- explicit component storage and queries;
- deferred lifecycle and deterministic update boundaries;
- prefab/component instantiation;
- components contain light, trigger, audio, physics, render, animation, and VFX
  references/data, while bridge systems synchronize with the owning modules;
- no universal entity object that directly performs every domain operation.

### CryPhysics

Historical responsibility:

- sphere, box, cylinder, ray, triangle-mesh, and height-field geometry;
- AABB/OBB trees, grids, intersections, overlap, and unprojection;
- broad and narrow collision;
- physical world, static/rigid/living/wheeled/rope/soft/particle/articulated
  entities;
- stepping, queries, explosions, surfaces, events, and serialization.

The current official
[Physics System Programming](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1605653)
documentation likewise separates geometry creation, physical entities, the
physical world, stable association IDs, and world queries.

Cypher decision:

- one `CypherPhysics` owner for collision shapes, broad phase, narrow phase,
  casts/sweeps, contact records, fixed stepping, solver, bodies, layers/masks,
  character movement, and projectile collision;
- share only domain-neutral primitive math with Mathlib;
- do not create a separate top-level collision engine until a proven independent
  query library has multiple consumers and a stable contract;
- keep World visibility indexes and Physics broad phase separate even though
  both use bounds.

### CryAISystem

Historical responsibility:

- AI objects for players, puppets, and vehicles;
- navigation graph data and triangulation;
- path search and heuristics;
- goal pipes/operations, formations, signals, perception, and behavior glue;
- debugging, memory reporting, and game-balance support.

Cypher decision:

- `CypherAI` owns navigation runtime queries, agents, pathing, perception, and
  behavior;
- an offline compiler builds navigation data;
- World places/references spatial navigation assets;
- entity components identify agents;
- World must not depend on high-level AI behavior.

### CryAnimation

Historical responsibility:

- skeleton/model loading, clips, controllers, interpolation, compressed curves;
- skins, morphs, bone hierarchy, vertex bindings;
- IK/procedural effectors, character instances, playback;
- physics/ragdoll and renderer integration;
- character-attached effects, decals, trails, particles, and lights.

Cypher decision:

- `CypherAnimation` owns skeletons, clips, poses, blending/graph state, root
  motion, events, IK, and animation resource residency;
- renderer and physics consume explicit pose/adapter data;
- Animation does not create native GPU or physics objects directly.

### CryFont

Historical responsibility:

- named fonts and TrueType loading;
- FreeType glyph rasterization;
- glyph bitmap/cache and texture-atlas allocation;
- Unicode/wide-character measurement and drawing;
- wrapping, clipping, scaling, and configured multipass effects.

Current official [`IFFont`](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/28704770/pages/29797240)
documentation exposes font loading, UTF-8 drawing, measurement, wrapping, and
renderer-thread integration, confirming that font behavior remained a distinct
runtime service.

Cypher decision:

- create `CypherFont` for face/family/fallback state, shaping input/output,
  metrics, measurement, rasterized/cooked glyph data, bounded cache/atlas policy,
  and renderer-neutral text batches;
- use a shaping solution capable of ligatures, combining marks, bidirectional
  text, script selection, and fallback before freezing a public layout API;
- let Render upload atlas pages and execute prepared batches;
- let UI own widget layout, not glyph layout internals.

### CryInput

Historical responsibility:

- keyboard, mouse, joystick/gamepad, and platform device handling;
- buffered/event input;
- action maps and bindings.

Cypher decision:

- `CypherSystem` emits normalized raw platform events;
- `CypherInput` owns per-frame device state, focus-loss cleanup, action maps,
  contexts, chords, axes, dead zones, rebinding, and persistence;
- `.cyinput`/`.cybindings` or a future `cykeymap` family is consumed by Input,
  not by UI or gameplay directly.

### CrySoundSystem

Historical responsibility:

- decoded/static buffers and streaming voices;
- PCM, ADPCM, and Ogg/Vorbis decoding;
- 2D/3D sources, listener, attenuation, priorities, groups/channels;
- occlusion/visibility-area input;
- reverb environments and editor presets;
- dynamic music themes, moods, patterns, and transitions.

Cypher decision:

- `CypherAudio` owns devices, mixer, clips/streams, voices, buses, listener,
  spatialization, priority/stealing, events, acoustic inputs, and music state;
- World/entity components store placements and zones;
- World may provide occlusion/acoustic query inputs without owning the mixer.

### CryScriptSystem

Historical responsibility:

- embedded Lua 4.01 state;
- source loading, compilation, bytecode execution, reload, and garbage
  collection;
- values/objects, calls, stack guards, buffers, userdata, allocation;
- debugging/error sinks and virtual filesystem integration.

Cypher decision:

- `CypherScript` owns the VM, modules, values/handles, marshaling, lifetime,
  sandbox policy, diagnostics, and reload boundary;
- game-specific bindings remain with their owning game/system adapters;
- scripts receive capability-scoped APIs rather than a global engine locator.

### CryMovie

Historical responsibility:

- sequences, nodes, tracks, and keyframes;
- camera, entity, character, material, CVar, command, event, sound, music,
  selection, expression, and script tracks;
- playback, editor callbacks, and XML persistence.

Cypher decision:

- coordinate early sequences from Game/Animation/Audio/UI;
- create `CypherCinematics` only when Mason and runtime share a real versioned
  timeline asset, evaluator, playback clock, events, tests, and at least one
  authored sequence.

### CryNetwork

Historical responsibility:

- UDP/datagram sockets and endpoint state machines;
- client, server, local-client, and server-slot state;
- reliable/control packet handling and compression;
- browsing, remote console, network simulation, probes;
- anti-cheat and online-service integration.

Cypher decision:

- `CypherNetwork` owns transport, packets, channels, connection/session state,
  reliability, sequencing, and measured snapshot transport;
- REAP component replication, prediction, and authority rules remain gameplay
  or explicit networking adapters;
- remote diagnostics use a versioned telemetry protocol rather than direct
  access to game/runtime objects.

### CryGame

Historical responsibility:

- level flow, single-player and multiplayer rules;
- client/server integration;
- players, enemies, vehicles, weapons, flocks, spectators;
- serialization and synchronized tables;
- material/surface glue, mods, scripts, localization, time demos;
- editor/game-mode behavior;
- HUD, radar, menus, widgets, and video panels;
- cutscene callbacks and much of the main loop.

Cypher decision:

- `CypherGame` owns REAP-specific rules, actors, weapons, waves, session/level
  flow, gameplay state, and gameplay serialization;
- Host owns the schedule and lifecycle;
- `CypherUI` owns reusable runtime UI machinery;
- engine compilers and runtime services remain outside Game.

### Editor

The historical editor combined many authoring domains in one MFC executable:

- orthographic, perspective, model, and terrain viewports;
- object creation, selection, clone, link, groups, layers, gizmos, properties;
- terrain sculpting, holes, beaches, lighting, textures, vegetation, water;
- brush/plane/polygon/face and indoor/visibility authoring;
- entities, prototypes, prefabs, volumes, areas, audio, AI points, cameras;
- material, shader, particle, music, sound, reverb, and equipment libraries;
- cinematic Track View, lightmap compilation, undo, plug-ins, export, and live
  game mode.

Cypher decision:

- adopt reuse of real runtime systems and live preview;
- keep tool document state, commands, undo, selection, inspectors, and Qt UI in
  TileEditor/Mason;
- compose focused libraries and tools instead of creating one editor-owned copy
  of every runtime behavior;
- runtime code never depends on Qt.

### Resource compiler family

The historical resource compiler used one orchestration executable with
converter plug-ins. It parsed build settings, mapped extensions to converters,
selected platforms, performed incremental checks, wrote dependencies and
diagnostics, and delegated geometry/image work to compiler modules.

Current official CryEngine documentation still presents the
[Resource Compiler](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/44959586)
as the source-to-runtime conversion boundary.

Cypher decision:

- retain `CypherResourceCompiler` as orchestration and typed format compilers as
  owned conversion modules;
- use semantic/content hashes, an explicit dependency graph, versioned cache
  keys, deterministic output, atomic publication, structured diagnostics,
  validation-only mode, response files, and clean/incremental equivalence tests;
- avoid timestamp-only correctness and implicit plug-in discovery.

## Where The Apparently Missing Features Belong

CryEngine 1 did not provide one DLL for every feature. The following mapping is
the actionable result for Cypher:

| Feature | Historical placement | Cypher ownership |
| --- | --- | --- |
| entity/component lifecycle | broad `CryEntitySystem` entity object | `CypherEntity` IDs, stores, queries, lifecycle, systems, prefab instantiation |
| triggers and areas | entity areas, editor volumes, physics overlap, game scripts | Entity trigger component; Physics overlap/query; World spatial registration; Game response |
| collision detection | `CryPhysics` | `CypherPhysics`, with internal reusable query code where justified |
| rigid/character/projectile physics | `CryPhysics` plus entity/game adapters | `CypherPhysics`, synchronized through ECS bridge systems |
| lights | entity data and `Cry3DEngine`, executed by renderer | Entity component; World spatial selection; renderer execution |
| visibility | terrain/object sectors, vis areas, portals, renderer refinement | World zones/portals/PVS and CPU extraction; renderer-side fine/GPU culling |
| terrain | `Cry3DEngine` | `CypherWorld/Terrain` |
| static objects | `Cry3DEngine` | `CypherWorld` placement and spatial records |
| vegetation | `Cry3DEngine` | World environment placement; graduate only with substantial independent simulation/tooling |
| water/environment volumes | `Cry3DEngine` plus physics | World records; Physics buoyancy/collision; renderer execution |
| particles | 3D engine, animation attachments, editor library | World/entity placement plus simulation/extraction; future `CypherVFX` when independently justified |
| decals | 3D engine and editor | World persistent placement; effect lifetime may graduate with VFX |
| runtime HUD/menu | `CryGame` | `CypherUI`; Qt stays in tools |
| fonts | `CryFont` | `CypherFont` |
| audio | `CrySoundSystem`, with entity/world placements | `CypherAudio`; Entity/World hold components/zones |
| animation | `CryAnimation` | `CypherAnimation`; ECS stores state and renderer consumes poses |
| navigation/AI | `CryAISystem` plus editor-exported data | `CypherAI`; compiler builds data and World references it |
| cinematics | `CryMovie` plus Track View | coordinated initially; later `CypherCinematics` after a real timeline slice |
| input/key maps | `CryInput` | `CypherInput`; authored bindings compile into an Input-owned format |
| streaming | central stream engine, domain-specific policies | shared async I/O/job mechanism; Resource/World/Audio own residency policy |
| networking | low-level `CryNetwork`, higher-level `CryGame` | Network transport/session; Game/adapter replication schemas |

## Current Cypher Gap Map

The maturity words below use ADR 0006.

| Cypher boundary | Current state | Evidence now | First missing vertical slice |
| --- | --- | --- | --- |
| Common | implemented, broad | explicit tiered targets, formats, utilities, math, tests | remove duplicate legacy foundations as consumers migrate |
| System | implemented/integrated | OS paths/time/events/windows/contexts/VM | profile-safe headless/event paths as new consumers arrive |
| Host | implemented/integrated | ordered core startup and process loop | explicit client/server/test profiles and full subsystem rollback matrix |
| Log/Memory/Pak/FileSystem | implemented/integrated | explicit targets and focused tests | Common-VFS adapter and behavior-preserving consolidation |
| Command/CVar/Config | implemented legacy path | startup config and built-ins | migrate Host to the instance-owned Common Tier1 command system |
| Resource | partial | handles/managers and first cooked render adapter | dependency graph, async ownership, eviction/reload policy |
| World | scaffold | accepted ownership and format/tool planning | generational spatial records, bounds updates, linear visibility, immutable submission |
| Entity | planned | formats and architecture only | IDs, component stores, spawn/deferred destroy, transform bridge, deterministic update |
| Physics | planned | Mathlib primitives only | static colliders, ray/shape/sweep queries, player capsule movement, debug capture |
| Input | planned | System raw events and format proposal | per-frame devices, focus loss, actions, one movement/look consumer |
| Audio | planned | formats/architecture only | headless mixer/device abstraction, clip/stream, voice, listener/emitter, failure recovery |
| Font | scaffold | accepted boundary | cooked face, shaping/fallback, measurement, bounded atlas, one visible line |
| UI | scaffold | accepted boundary | one HUD/menu, layout, focus/navigation, clipping, text, bounded draw list |
| Animation | planned | no runtime slice | one skeleton, clip, pose sampling/blend, render handoff |
| AI | planned | no runtime slice | navigation asset/query and one agent consumer |
| VFX | planned responsibility | no target by design | one versioned effect, deterministic emitter simulation, bounds, extraction, test scene |
| Script | planned | no VM integration | VM/value/native-call contract used by one gameplay behavior |
| Network | planned | formats/architecture only | transport/session framing and one measured snapshot path |
| Game | planned | no REAP runtime loop | controllable player, target, damage, wave, reset, and headless smoke |

## Adopt, Adapt, Or Reject

| CryEngine 1 decision | Cypher choice | Reason |
| --- | --- | --- |
| thin executable delegates to one lifecycle owner | **adopt** | process/runtime responsibilities remain reviewable |
| one owner defines init, frame, rollback, shutdown | **adopt** | partial failure and lifetime order become testable |
| dedicated/editor/preview profiles | **adopt** | supports server, tools, tests, and client without fake initialization |
| null renderer/headless capability | **adopt capability** | simulation and tools must run without a visible GPU |
| editor reuses runtime systems | **adopt** | prevents editor/runtime behavior drift |
| offline source-to-runtime compiler | **adopt and modernize** | deterministic production assets need an explicit build boundary |
| converter plug-in registration | **adapt** | typed registry/manifests first; dynamic loading only where useful |
| all public interfaces in Common | **adapt** | common primitives stay central; domain APIs remain with owners |
| separate DLL for nearly every subsystem | **reject as default** | static targets are simpler until ABI replacement is required |
| C exports returning C++ virtual interfaces | **reject for stable ABI** | compiler/STL/vtable layout is fragile |
| header timestamp as compatibility version | **reject** | use explicit API/data versions and validated structures |
| global system/service locator | **reject** | hidden dependencies create cycles and obstruct testing |
| per-module global system pointer | **reject** | prevents clear ownership and multiple isolated instances |
| `Cry3DEngine` world/render/effects umbrella | **adapt** | World remains precise; Physics/Animation/VFX/Render have clear owners |
| entity god object | **reject** | components and bridge systems keep domain lifetimes separate |
| game DLL owns main loop and nearly every integration | **reject** | Host schedules; Game owns rules |
| runtime UI buried in game code | **reject** | shared Font/UI are real runtime services |
| narrow font module | **adopt** | text owns independent data, cache, shaping, and measurement |
| transport separate from game rules | **adopt and sharpen** | replication schemas and transport have different change rates |
| central I/O scheduler plus domain streaming policy | **adapt** | share scheduling while each domain owns residency choices |
| monolithic editor | **reject** | tools compose document/runtime services and remain Qt-owned |
| duplicated vendor sources | **reject** | one reviewed version, adapter, and license record per dependency |
| minimal first-party automated verification | **reject** | tests and benchmarks are part of subsystem completion |
| rename World to match historical terminology | **reject** | names express current ownership, not lineage |

## Dependency Direction

The resulting dependency direction is:

```text
Common / Math / System
          |
          v
Log / Memory / Pak / FileSystem / Jobs
          |
          v
Command / CVar / Config / Resource
          |
          v
Input / Entity / Physics / Audio / Font / Animation / AI / Script / Network
          |
          v
World / UI / Game and explicit bridge systems
          |
          v
Host composition, selected renderer execution, product entry point
```

This is a conceptual layering guide rather than a command that every lower row
must depend directly on every higher row. Handles, immutable snapshots, bounded
queues, events, and explicit service inputs should cross ownership boundaries.
Cycles such as Entity to Physics to World back to Entity must be broken by a
bridge assembled at the Host/Game layer.

## Verification Missing From The Historical Model

The pinned tree has no meaningful first-party unit-test or benchmark product;
the substantial test directories belong to bundled STLport. Cypher should make
the following evidence part of the definition of each system:

- Math: convention tests, randomized identities, finite behavior, reference
  geometry comparisons, scalar/SIMD parity, and microbenchmarks.
- Entity: stale handles, generation policy, churn, deferred destruction,
  deterministic queries, component movement, and iteration throughput.
- Physics: primitive pairs, broad-phase equivalence to brute force, casts and
  sweeps, contact invariants, fixed-step behavior, and worst-case scenes.
- World: insert/update/remove, conservative visibility, portal/PVS behavior,
  streaming transitions, serialization, deterministic extraction, and large
  spatial-query benchmarks.
- Audio: headless voice allocation, priority/stealing, stream underrun/failure,
  spatial parameter tests, and callback-thread ownership.
- VFX: seeded simulation, conservative bounds, emitter lifecycle, overflow, and
  update/extraction throughput.
- Font/UI: Unicode shaping/fallback, measurement, cache eviction, atlas pressure,
  layout, hit testing, focus/navigation, clipping, and text-layout benchmarks.
- Asset tools: golden outputs, dependency invalidation, stable hashes, malformed
  input, clean/incremental equivalence, atomic failure, and parallel builds.
- Host: client, server, editor, test, no-audio, no-window, and injected-failure
  startup/shutdown matrices.
- Integration: a deterministic headless REAP smoke exercising World, Entity,
  Physics, Input commands, AI/script hooks, and audio-event production without
  renderer ownership.

## Implementation Sequence

The structure is now fixed enough to proceed without mass-producing folders:

1. Maintain the explicit target ownership introduced by ADR 0006. Remove legacy
   duplication only after behavior parity is proven.
2. Build one headless `Entity + World + Physics` slice: spawn a generation-safe
   object, register bounds and a static collider, sweep a player/projectile,
   update the transform, query visibility, destroy it, and reject stale handles.
3. Integrate `CypherInput` with System events and use the authored input format
   for one movement/look consumer.
4. Add `CypherAudio` with a headless test backend and one spatial emitter so
   client/server profiles can prove correct optional ownership.
5. Add cooked font data and `CypherFont`, then one focused `CypherUI` HUD/menu
   using Font and Input.
6. Add animation and navigation only around the first actual actor/enemy needs.
7. Promote VFX into its own target only with an effect schema, simulation,
   extraction, caller, test scene, and benchmarks.
8. Add scripting and networking around concrete gameplay contracts.
9. Let Mason consume the proven runtime APIs and cooked schemas. Tool UI must
   not become the owner of runtime behavior.

A proposed top-level target must pass all of these checks:

- it owns distinct long-lived state or a coherent processing domain;
- it has a narrow public contract and an allowed dependency list;
- it has a real runtime or tool caller;
- it has meaningful correctness or performance evidence;
- its lifetime can be created, failed, rolled back, and destroyed independently
  in every relevant Host profile.

## Primary And Corroborating Sources

- Crytek, *Far Cry Engine Overview*, Revision 6 (2005):
  [indexed PDF URL](https://fcdb.crymods.net/resources/pdf/sdk/Far%20Cry%20Engine%20Overview.pdf)
- Preserved official Far Cry Mod SDK 1.4 readme:
  [SDK 1.4 archive page](https://www.gamepressure.com/download/far-cry-sdk-v14-mod/zf35eb)
- Crytek, current public API orientation:
  [Introduction to the Engine API](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/26874885)
- Crytek, font interface:
  [`IFFont`](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/28704770/pages/29797240)
- Crytek, physics architecture:
  [Physics System Programming](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1605653)
- Crytek, resource compilation:
  [Resource Compiler](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/44959586)
- Unlicensed structural mirror, pinned and excluded from implementation use:
  [`OpenFarCry1` commit `38da5286`](https://github.com/OpenFarCry1/Far-Cry-1-CryEngine1/tree/38da5286bc420aa716662763cbde398473abe893)
- Corroborating mirror that identifies itself as leaked, excluded from
  implementation use:
  [`StrongPC123` commit `1a41d18c`](https://github.com/StrongPC123/Far-Cry-1-Source-Full/tree/1a41d18ccd9b6a7071a72291d366ec85e66c48c7)
