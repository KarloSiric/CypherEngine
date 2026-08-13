<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/tool_suite.md
//  Purpose: Defines the complete planned CypherEngine authoring and tooling suite.
//  Details: This document inventories interactive Qt applications, Mason
//           workspaces, command-line processors, diagnostics, and build services.
//           It records working names and responsibilities without requiring tools
//           to be implemented before their runtime data contracts exist.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Tool Suite

## Purpose

This is the authoritative inventory of planned CypherEngine tools.

The long-term objective is a complete cross-platform production environment for
building, inspecting, debugging, cooking, testing, and shipping Cypher games.
The objective is not to reproduce every historical Valve executable. Cypher
should provide the useful production capability through a smaller, coherent
architecture with shared implementation code.

The suite targets Windows, Linux, and macOS.

`Mason` is the authoritative product name. When a formal long form is useful,
use **MASON: Map and Scene Operations Nexus**. Normal prose and identifiers
should continue to use `Mason`; the backronym is descriptive, not a requirement
to capitalize every reference.

## Product Rules

1. Interactive desktop authoring and inspection products use Qt 6.
2. Mason is the primary all-in-one authoring application.
3. A Mason workspace may also have a focused standalone launch mode.
4. Headless compilers, validators, and automation tools do not depend on Qt.
5. Runtime debug overlays use Dear ImGui and are not desktop authoring tools.
6. GUI actions and CLI commands call the same shared compiler libraries.
7. The engine owns rendering, simulation, resources, and world data. Qt owns
   desktop windows, docking, menus, property controls, and native integration.
8. A tool begins only after its runtime contract and source/cooked data model are
   sufficiently real to edit or inspect.
9. Every compiler must be deterministic, scriptable, and usable without Mason.
10. No critical compiler behavior may exist only inside a Qt callback.

## Tool Naming Policy

Cypher tools use full descriptive names as their authoritative product and
executable names. Abbreviations such as `cymapc`, `cyshaderc`, and `cypkgcli`
are not canonical names. They save little typing while making logs, process
lists, crash reports, build targets, and documentation harder to understand.

The naming layers are:

| Layer | Example | Rule |
| --- | --- | --- |
| Product | `CypherMapCompiler` | Public name used in documentation and diagnostics. |
| Executable/CMake target | `CypherMapCompiler` | Exact cross-platform basename; Windows adds `.exe`. |
| Reusable implementation library | `CypherMapCompilerCore` | Contains compiler behavior shared by the CLI and Mason. |
| CMake alias | `Cypher::MapCompilerCore` | Namespaced link target for dependent code. |

Compiler and processor names normally use one of these role suffixes:

- `Compiler` transforms authored source into validated or cooked output.
- `Validator` checks data without transforming it.
- `Inspector` reads and reports without mutating the input.
- `Converter` changes representation while preserving meaning.
- `Manager` performs a persistent administrative workflow.
- `Runner` orchestrates tests, benchmarks, or another bounded workload.
- `Server` identifies a long-running service process.

Do not append `CLI` merely because a product is command-line driven. The
execution environment already establishes that fact. For example, use
`CypherDoctor` and `CypherPak`, not `DoctorCLI` or `cypkgcli`.

Distinctive names are reserved for user-facing products whose identity benefits
from one, such as `Mason` and `CypherScope`. Community tools around Valve games
demonstrate that memorable names can work well, but Cypher names must remain
original and their responsibilities must still be obvious in documentation.

Working names remain changeable until the corresponding target and ownership
boundary exist. Renaming an implemented executable requires updating CMake,
automation, package manifests, documentation, and compatibility policy together.

```text
                         Shared tool libraries
                   import, validate, compile, inspect
                         /        |        \
                        /         |         \
               Mason workspace   Qt app    headless CLI
                        \         |         /
                         \        |        /
                        Cypher public contracts
                                 |
             runtime resources, world, renderer, simulation
```

## Status Vocabulary

| Status | Meaning |
| --- | --- |
| Decided | Product direction and responsibility are accepted. |
| Planned | Capability is accepted but detailed design is deferred. |
| Proposed | Useful candidate that still needs a concrete production requirement. |
| Deferred | Intentionally late because prerequisite systems do not yet exist. |
| Existing foundation | Some supporting runtime or build code already exists. |

Status describes the design decision, not implementation completion.

## Source 1 Comparison

The Source 1 tool list evolved across multiple engine branches. Not every tool
was present in the original 2004 SDK, and several tools were game-specific or
legacy conversion utilities. The comparison identifies useful responsibilities,
not code or UI to copy.

| Source 1 tool or capability | Cypher equivalent | Delivery | Decision | Responsibility |
| --- | --- | --- | --- | --- |
| Source SDK launcher/configuration | CypherProject | Qt 6 application | Proposed | Create, open, validate, and configure engine/game projects. |
| Hammer | Mason Map and World workspace | Mason plus focused Qt launch | Decided | Author maps, scenes, geometry, entities, layers, prefabs, lighting, and gameplay volumes. |
| VBSP | CypherMapCompiler geometry stage | Headless CLI/library | Decided | Compile source geometry and world records into runtime-ready structures. |
| VVIS | CypherMapCompiler visibility stage | Headless CLI/library | Planned | Build portals, cells, PVS data, and other visibility acceleration. |
| VRAD | CypherMapCompiler lighting stage | Headless CLI/library | Planned | Bake lightmaps, probes, static lighting, and diagnostics. |
| VMPI | CypherBuildWorker | Headless service | Deferred | Distribute expensive cooking, lighting, shader, and validation work. |
| BSPZip | CypherPak map/package stage | Headless CLI/library | Planned | Attach or package map dependencies through the normal package pipeline. |
| BSP inspection tools | CypherScope World Inspector | Qt 6 application mode | Planned | Inspect cooked world chunks, visibility, collision, entities, and dependencies. |
| StudioMDL | CypherModelCompiler | Headless CLI/library | Decided | Compile meshes, skeletons, animation clips, hitboxes, attachments, LODs, and morph targets. |
| Half-Life Model Viewer | Mason Model workspace and CypherScope | Mason/focused Qt launch | Decided | Preview models, materials, bones, animations, hitboxes, collision, sockets, and LODs. |
| Model Browser | Mason Asset Browser | Mason workspace | Decided | Search, filter, preview, place, and reference model and resource assets. |
| QC Eyes | Mason Character Setup workspace | Mason workspace | Planned | Configure eye placement, gaze limits, head targeting, mouth setup, and facial metadata. |
| Faceposer | Mason Choreography workspace | Mason/focused Qt launch | Planned | Author dialogue, phonemes, facial poses, gestures, actors, cameras, and timed gameplay events. |
| DMXConvert/DMXEdit | CYKV Convert and Migrate tools | Headless CLI plus CypherScope | Planned | Convert, normalize, migrate, inspect, and compare structured source documents. |
| VTEX | CypherTextureCompiler | Headless CLI/library | Implemented v1 | Import PNG/JPEG/EXR and cook canonical mip chains and color-space metadata; compression and platform variants are later versions. |
| VTF2TGA and texture utilities | CypherImageConverter | Headless CLI/library | Planned | Convert and inspect supported source and cooked image representations. |
| Height2Normal/Height2SSBump | Mason Texture Lab | Mason/focused Qt launch | Planned | Generate and preview normal, height, mask, and derived material textures. |
| Material Editor | Picasso Material workspace and Mason Material workspace | Picasso Qt app plus Mason workspace | Decided | Author typed materials and previews through one shared editor core; graphs, render states, and reflection arrive with renderer contracts. |
| ShaderCompile | CypherShaderCompiler | Headless CLI/library | Implemented v1 | Preprocess, parse, link, and deterministically cook bounded OpenGL GLSL graphics programs; reflection and permutations are later versions. |
| Particle Editor | Mason VFX workspace | Mason/focused Qt launch | Decided | Author particle systems, emitters, curves, modules, events, and previews. |
| VSoundEdit | Mason Audio workspace | Mason/focused Qt launch | Decided | Edit sound assets, loops, spatial properties, buses, mixers, soundscapes, and reverb zones. |
| CaptionCompiler | CypherCaptionCompiler | Headless CLI/library | Planned | Validate and compile subtitle, closed-caption, timing, and localization data. |
| ActBusy Script Editor | Mason AI and Choreography workspaces | Mason workspace | Planned | Author ambient behaviors, schedules, interactions, and staged NPC activity. |
| Entity Placement Tool | Mason Entity and Prefab workspace | Mason workspace | Decided | Place, configure, group, instance, and validate gameplay entities. |
| Commentary Editor | Mason Annotation workspace | Mason workspace | Proposed | Author developer commentary, tutorials, review markers, and internal annotations. |
| Particle/Material/Post tools | Mason VFX, Material, and PostFX workspaces | Mason/focused Qt launch | Planned | Centralize rendering-effect authoring without duplicating renderer logic. |
| Source Filmmaker | Mason Cinematic workspace | Mason/focused Qt launch | Deferred | Author cameras, actors, animation, dialogue, lighting, sequencing, and final captures. |
| Element Viewer | CypherScope | Qt 6 application | Decided | Inspect every editable and cooked Cypher format through schema-aware views. |
| DemoInfo | CypherReplayInspector | Qt 6 application mode plus CLI | Planned | Inspect replay headers, packets, commands, snapshots, events, and timing. |
| VConsole | CypherConsole | Qt 6 application | Planned | Receive remote logs, commands, CVars, channels, filters, and live diagnostics. |
| VPK | CypherPak | Library plus headless CLI | Existing foundation | Create, list, extract, verify, sign, diff, and inspect `.cypak` archives. |
| MakeGameData/FGD workflow | CypherSchemaCompiler | Headless CLI/library | Planned | Compile reflected entity/property schemas for runtime validation and Mason inspectors. |
| VPC | CypherBuilder | Headless CLI | Planned | Generate/configure projects and orchestrate reproducible local and CI builds. |

References:

- [Official Source SDK tools](https://developer.valvesoftware.com/wiki/Template:Sdktools/doc)
- [Faceposer](https://developer.valvesoftware.com/wiki/Face_Poser)
- [Valve Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013)

## Source 2 Capability Comparison

The Source 2 comparison focuses on production responsibilities rather than
matching Valve's executables or extensions one for one. The detailed research,
format-family inventory, evidence limits, and architectural conclusions are in
[source2_tooling_reference.md](source2_tooling_reference.md).

| Source 2 tool or capability | Cypher equivalent | Decision | Principal lesson |
| --- | --- | --- | --- |
| Asset Browser | Mason Asset Browser | Decided | Asset type metadata must dispatch previews, validators, compilers, and associated editors. |
| Hammer 5.x | Mason Map and World workspace | Decided | Geometry, entities, visibility, collision, lighting, build, and play are coordinated but independently compiled responsibilities. |
| Material Editor | Picasso Material workspace and Mason Material workspace | Decided | Material source, resource references, compilation, and live preview share compiler/editor-core libraries and the real renderer path. |
| ModelDoc | Mason Model and Character Setup workspaces | Decided | Mason assembles engine-specific model data; it does not replace a general DCC application. |
| Animgraph Editor | Mason Animation Graph workspace | Decided | Parameters, state machines, blends, events, IK, authority, and graph debugging require a compiler and runtime evaluator. |
| Particle Editor | Mason VFX workspace | Decided | Typed phase-aware modules and preview controls must remain independent from Qt widgets. |
| Post Processing Editor | Mason PostFX and Color workspace | Planned | Preserve editable layers and masks even when cooking bakes lower-cost LUT data. |
| Image Subrect Editor | Mason Texture Lab atlas/subrect mode | Planned | Sprite regions, atlas rectangles, pivots, and material hotspots need explicit source data and preview. |
| VConsole | CypherConsole | Planned | External structured logs, commands, channels, filters, and plugin telemetry remain usable when the game fails. |
| Resource Compiler | CypherResourceCompiler | Decided | One coordinator dispatches deterministic type-specific compiler libraries and dependency-aware builds. |
| Workshop Manager | Mason Mod/Release workspace and CypherModManager | Planned | Package validation and publishing are adapters over the normal package pipeline. |
| CS2 Workshop Item Editor | Game-specific Cypher tool only if required | Deferred | Economy and skin workflows do not belong in the general engine by default. |
| Source 2 Filmmaker | Mason Cinematic workspace | Deferred | Cameras, shots, actors, animation, audio, effects, recording, and output form a late production subsystem. |
| VData-style generic assets | Mason Data and Schema workspace | Planned | Schema-selected generic source data can serve gameplay records without bespoke editors for every type. |
| VMix/sound event workflows | Mason Audio Event and Mix Graph modes | Planned | Samples, events, rule stacks, spatial metadata, and mix graphs are separate authored products. |
| Pulse/smart-prop workflows | Mason Flow and Procedural Object modes | Deferred | Visual logic and parametric object rules are useful only after their runtime consumers exist. |

The Source2 Wiki is community-maintained and contains incomplete pages. This
table records only publicly established responsibilities and explicit Cypher
decisions; it is not a claim that the public pages describe Valve's complete
internal toolchain.

## Qt 6 Desktop Products

All products in this section are cross-platform Qt 6 applications. Focused tools
may share the Mason executable through a workspace launch argument until a
separate executable provides a proven workflow benefit.

| Product | Status | Primary purpose | Relationship to Mason |
| --- | --- | --- | --- |
| Mason | Decided | Main engine editor and authoring shell. | Hosts all normal authoring workspaces. |
| Picasso | V1 design approved, implementation deferred | Focused texture and material authoring, import, mip/channel inspection, typed bindings, validation, compilation, and preview. | One dense Hammer-influenced Qt 6 shell hosts separate `TextureEditorCore` and `MaterialEditorCore` workspaces; Mason later reuses both cores. The visual and workflow contract is defined in [picasso_v1_design.md](picasso_v1_design.md); implementation begins after the first real renderer preview provider. |
| CypherProject | Proposed | Project creation, SDK discovery, build profiles, recent projects, and launch configuration. | Launches Mason, builds, games, servers, and tools. |
| CypherScope | Decided | General source/cooked resource, package, schema, and dependency inspector. | Opens from Asset Browser or runs standalone. |
| CypherConsole | Planned | External log, command, CVar, channel, and remote-session console. | Embeddable as a Mason panel, useful standalone during crashes. |
| CypherProfiler | Planned | CPU/GPU frame, task, allocation, IO, resource, and timeline analysis. | Embeddable summary in Mason; deep traces open standalone. |
| CypherReplayInspector | Planned | Replay, demo, snapshot, packet, event, and deterministic-simulation inspection. | Launches from playtest and network workspaces. |
| CypherNetworkInspector | Planned | Connections, packet flow, channels, loss simulation, replication, and prediction diagnostics. | Embeddable during playtest; focused standalone sessions supported. |
| CypherMemoryInspector | Planned | Allocators, pools, tags, ownership, leaks, fragmentation, and snapshots. | Receives live telemetry from engine/editor processes. |
| CypherCrashViewer | Planned | Crash reports, minidumps, stack traces, logs, build IDs, and attached diagnostics. | Independent so it remains usable when Mason or the engine fails. |
| CypherBuildMonitor | Proposed | Observe local/remote cooking, shader compilation, cache activity, and worker health. | Mason build panel provides the common view. |
| CypherPackageManager | Proposed | Visual package composition, signing, dependency review, patch creation, and publishing. | Uses CypherScope and CypherPak; may remain a Mason workspace. |
| CypherTestLab | Proposed | Run visual, map, rendering, physics, replay, and asset regression suites. | Integrates test results with affected assets and scenes. |
| CypherBenchmarkViewer | Proposed | Compare benchmark JSON, machines, builds, regressions, variance, and historical baselines. | Mason shows summaries; detailed comparison can run standalone. |
| CypherServerConsole | Planned | Administer dedicated servers, sessions, maps, players, moderation, logs, and performance. | Reuses CypherConsole and network telemetry components. |
| CypherModManager | Planned | Create, validate, package, sign, install, enable, disable, and publish mods/add-ons. | Integrates with Mason projects, CypherPak, and release validation. |

## Mason Workspace Inventory

### Foundation Workspaces

| Workspace | Decision | Required capabilities |
| --- | --- | --- |
| Project | Planned | Project settings, target profiles, plugin/module configuration, launch and build profiles. |
| Asset Browser | Decided | Search core/project/add-on content, thumbnails, tags, previews, dependency graph, references, rename/move repair, import, cook status, source control state, and associated-editor dispatch. |
| Scene Hierarchy | Decided | Stable object IDs, parenting, layers, visibility, locking, grouping, filtering, and selection synchronization. |
| Inspector | Decided | Schema-driven properties, validation, units, ranges, resource pickers, multi-edit, defaults, and overrides. |
| Console and Output | Decided | Logs, commands, CVars, compiler diagnostics, filtering, links, and remote process selection. |
| Build and Cook | Decided | Incremental compilation, platform/profile selection, cache status, progress, cancellation, and diagnostics. |
| Playtest | Decided | Play, pause, frame-step, stop, possess, reload, simulation isolation, and optional out-of-process launch. |
| Profiler | Planned | Frame summary, CPU/GPU timings, allocations, IO, renderer statistics, and trace capture. |
| Source Control and Review | Planned | Change lists, diff, history, locks where required, conflict awareness, annotations, and review links without replacing the source-control client. |

### World And Gameplay Workspaces

| Workspace | Decision | Required capabilities |
| --- | --- | --- |
| Map and World | Decided | Four-view and 3D layouts, brushes, editable meshes, instances, layers, snapping, UVs, entities, triggers, volumes, compile, and play. |
| Entity and Prefab | Decided | Entity palettes, schemas, components, overrides, prefab creation, variants, nested instances, reference repair, and later parameterized/procedural object rules. |
| Objective and Flow | Decided | Events, conditions, actions, mission state, entity connections, graph validation, and runtime debugging. |
| Terrain and Foliage | Planned | Terrain sculpting, painting, masks, vegetation placement, density rules, collision, LOD, and streaming regions. |
| Lighting | Decided | Light placement, probes, lightmaps, shadow settings, bake controls, diagnostics, and preview. |
| Navigation | Decided | Navmesh regions, links, costs, exclusions, agents, generation, validation, and path queries. |
| AI Behavior | Planned | Perception, behavior trees/state/schedule data, squads, encounters, spawn rules, debug traces, and simulation controls. |
| Physics Lab | Decided | Shapes, constraints, materials, ragdolls, vehicles, queries, collision layers, stress tests, and deterministic playback. |

### Content Workspaces

| Workspace | Decision | Required capabilities |
| --- | --- | --- |
| Material | Decided | Material definitions, shader selection, parameters, textures, variants, render states, validation, and live preview. |
| Texture Lab | Planned | Import settings, channels, color space, mipmaps, compression, normal generation, atlases, sprite subrects, pivots, hotspots, cubemaps, and platform previews. |
| Model | Decided | Meshes, skeletons, materials, LODs, hitboxes, collision, sockets, bounds, morph targets, import, compile, and preview. |
| Animation | Decided | Clips, skeleton mapping, blending, state/animation graphs, typed parameters, tags/events, root motion, IK, retargeting, compression, network authority, and live graph debugging. |
| Character Setup | Planned | Eyes, gaze, head aim, mouth/jaw, flex controls, hit reactions, attachments, and character-specific metadata. |
| Choreography and Dialogue | Planned | Actors, speech, phonemes, facial poses, gestures, animation, cameras, events, subtitles, and timeline curves. |
| VFX | Decided | Phase-aware particle graphs/modules, emitters, initializers, operators, renderers, curves, control points, events, ribbons, decals, preview, budgets, deterministic random streams, and platform quality levels. |
| Audio | Decided | Waveform preview, trimming metadata, looping, samples, containers, events, rule stacks, emitters, attenuation, buses, mix graphs, effects, soundscapes, reverb, and profiling. |
| UI and HUD | Planned | CyGUI documents, layouts, styles, fonts, localization, animation, input navigation, safe areas, and live game preview. |
| Font and Localization | Planned | Font import, glyph coverage, fallback, shaping tests, strings, plurals, subtitles, captions, and locale validation. |
| Shader | Planned | Shader source metadata, permutations, includes, reflection, compile errors, disassembly, resource bindings, and preview fixtures. |
| PostFX and Color | Planned | Ordered source layers, masks, opacity, tone mapping, exposure, color grading, LUT baking, fog, bloom, camera effects, volumes, transitions, global preview, and render-buffer comparison views. |

### Systems And Data Workspaces

| Workspace | Decision | Required capabilities |
| --- | --- | --- |
| Script | Decided | Lua source editing integration, syntax/diagnostics, project search, native bindings, reload, breakpoints, watches, call stacks, and runtime debugging. |
| Data and Schema | Planned | CYKV trees/text, typed generic data assets, schemas, defaults, validation, migrations, canonical formatting, source/cooked comparison, semantic diff, and reference navigation. |
| Gameplay Data | Planned | Weapons, items, damage, movement, enemies, waves, difficulty, game modes, response rules, and other schema-defined game records without hard-coding one game into Mason. |
| Input and Actions | Planned | Action maps, contexts, devices, bindings, chords, dead zones, accessibility, conflicts, glyphs, and runtime testing. |
| Settings and CVars | Planned | Defaults, categories, ranges, persistence, platform profiles, launch settings, and developer/release visibility. |
| Plugin and Module | Planned | Discover modules, inspect contracts and versions, configure load order, validate dependencies, and diagnose compatibility. |
| Mod and Add-on | Planned | Mod manifests, dependencies, mounted content, overrides, permissions, package composition, validation, and publishing. |
| Dedicated Server | Planned | Server profiles, maps, rotations, game modes, bots, access policy, moderation, logs, remote commands, and live telemetry. |

### Sequence And Production Workspaces

| Workspace | Decision | Required capabilities |
| --- | --- | --- |
| Cinematic | Deferred | Multi-track sequencing, cameras, shots, actors, animation, puppeteering, facial controls, dialogue, sound, lighting, effects, recording, render capture, and output management. |
| Replay | Planned | Timeline playback, camera control, entity inspection, event markers, packet/snapshot views, and export. |
| Annotation and Review | Proposed | Comments, bookmarks, tasks, measurements, screenshots, review states, and developer commentary. |
| Package and Release | Planned | Build manifests, packages, patch sets, signatures, dependency closure, target profiles, and release validation. |

The working name `Quark` may be used later as the display name for the VFX or
particle workspace, but it is not locked. `Mason` and `CypherScope` are the
current locked product names.

## Headless Compiler And Processor Inventory

These tools are portable C++ command-line programs. They must support exit codes,
human-readable diagnostics, machine-readable reports, deterministic output, and
response/config files suitable for CI.

| Working executable | Status | Inputs | Outputs and responsibility |
| --- | --- | --- | --- |
| `CypherResourceCompiler` | Decided | Resource manifests, file lists, and source assets | Coordinate importer/compiler dispatch, recursive and dependency-aware builds, caches, target profiles, loose output, and package updates. |
| `CypherMapCompiler` | Decided | `.cymap` | Produce `.cymap_c` through geometry, entity, collision, visibility, lighting, nav, and packaging stages. |
| `CypherModelCompiler` | Decided | glTF/GLB and model source metadata | Produce `.cymesh_c`, `.cyskel_c`, collision, LOD, sockets, and morph metadata. |
| `CypherAnimationCompiler` | Planned | Animation source and skeleton mapping | Produce `.cyanim_c` with events, root motion, compression, and retarget data. |
| `CypherAnimationGraphCompiler` | Planned | Animation graph, subgraph, parameter, tag, and authority source | Validate graph ownership and produce a compact runtime evaluator resource. |
| `CypherTextureCompiler` | Implemented v1 | PNG/JPEG/EXR and `.cytex` metadata | Produce deterministic `.cytex_c` RGBA8/RGBA32F mip chains and color-space metadata; compression/KTX/platform variants remain deferred. |
| `CypherMaterialCompiler` | Implemented v1 | `.cymat` | Validate typed shader/texture recipes and produce canonical `.cymat_c`; reflected shader compatibility remains deferred. |
| `CypherShaderCompiler` | Implemented v1 | Desktop GLSL and `.cyshader` metadata | Preprocess, parse, cross-stage link, validate, and produce deterministic `.cyshader_c`; reflection/permutations remain deferred. |
| `CypherVFXCompiler` | Planned | Particle/VFX source documents | Validate modules and produce cooked effect data. |
| `CypherAudioCompiler` | Planned | WAV/FLAC/other approved sources and sound metadata | Normalize, encode, analyze loudness, build seek/stream data, and produce `.cysnd_c`. |
| `CypherAudioGraphCompiler` | Planned | Sound events, containers, rule stacks, buses, and mix graphs | Validate references and produce runtime event/mixer graph resources. |
| `CypherFontCompiler` | Planned | Font sources and locale manifests | Produce `.cyfont_c`, atlases, glyph maps, fallback, and shaping metadata. |
| `CypherCaptionCompiler` | Planned | Dialogue, subtitle, and localization source | Produce validated, timed, localized caption resources. |
| `CypherNavigationCompiler` | Planned | Map geometry and `.cynav` overrides | Produce `.cynav_c` meshes, links, regions, costs, and debug data. |
| `CypherPhysicsCompiler` | Planned | Meshes and `.cyphys` source | Produce `.cyphys_c` collision shapes, materials, constraints, and mass properties. |
| `CypherFlowCompiler` | Planned | `.cyflow` | Validate and compile mission/objective/event graphs. |
| `CypherUICompiler` | Planned | CyGUI layout/style source | Validate and compile runtime UI documents, localization references, and resources. |
| `CypherScriptCompiler` | Planned | Lua source and binding metadata | Validate syntax/bindings, optionally produce bytecode, generate debug metadata, and build script manifests. |
| `CypherInputCompiler` | Planned | Input/action source documents | Validate conflicts and produce platform-aware runtime action maps. |
| `CypherLocalizationCompiler` | Planned | Localization tables and locale manifests | Validate keys, placeholders, plurals, coverage, encoding, and produce cooked string resources. |
| `CypherSceneCompiler` | Planned | `.cyscene` | Compile scene instances, dependencies, streaming partitions, and runtime records. |
| `CypherPrefabCompiler` | Planned | `.cyprefab` | Validate inheritance/overrides and produce runtime prefab/entity templates. |
| `CypherPostFXCompiler` | Planned | PostFX layers, masks, LUT sources, exposure, and volume profiles | Preserve editable source while baking target-specific LUT and runtime post-process data. |
| `CypherDataCompiler` | Planned | Schema-selected CYKV generic data | Produce bounded typed runtime records for gameplay and project-defined data families. |
| `CypherPak` | Existing foundation | Cooked resources and manifests | Create, list, extract, verify, sign, diff, patch, and report package contents. |
| `CypherKeyValues` | Planned | CYKV documents | Parse, format, canonicalize, validate, query, convert, and print diagnostics. |
| `CypherSchemaCompiler` | Planned | Reflection and schema declarations | Produce schema registries, editor descriptors, validation data, and optional generated bindings. |
| `CypherValidator` | Planned | Projects, source assets, cooked assets, or packages | Run cross-resource and release-readiness validation. |
| `CypherDependencyInspector` | Planned | Asset/project graph | Print dependencies, reverse references, cycles, missing assets, and rebuild reasons. |
| `CypherFormatInspector` | Planned | Any supported cooked format | Print headers, chunks, IDs, offsets, hashes, dependencies, and schema-aware values. |
| `CypherDataDiff` | Planned | Two source/cooked resources or manifests | Produce semantic and binary comparison reports. |
| `CypherDataMigrator` | Planned | Older source documents | Apply explicit schema migrations and report lossy transformations. |
| `CypherCacheManager` | Planned | Derived-data cache | List, verify, prune, explain misses, and report storage use. |
| `CypherReplayInspector` | Planned | Replay/demo files | Verify, summarize, extract events, compare simulations, and aid automated regression. |
| `CypherDedicatedServer` | Planned | Server/game configuration | Launch a dedicated authoritative game server. |
| `CypherServerControl` | Planned | Local or remote server endpoint | Query status, send commands, rotate maps, manage sessions, and export diagnostics. |
| `CypherModManager` | Planned | Mod/add-on source and manifest | Create, validate, package, install, list, and inspect mods and dependency closure. |
| `CypherPublisher` | Deferred | Validated package/release manifest | Stage signed releases or mods for approved distribution backends. |
| `CypherSymbolManager` | Planned | Native binaries and debug symbols | Index, verify, package, and resolve symbols for crash and profiler reports. |
| `CypherTestRunner` | Planned | Test profile or project | Orchestrate unit, integration, visual, replay, asset, and tool regression tests. |
| `CypherBenchmarkRunner` | Planned | Benchmark profile and JSON results | Run controlled benchmarks, capture machine metadata, compare baselines, and report regressions without using shared CI timing as truth. |
| `CypherProject` | Proposed | Project template or existing project | Create, validate, upgrade, and print project configuration. |
| `CypherBuilder` | Planned | Project/build profile | Configure, build, test, benchmark, package, and orchestrate supported targets. |
| `CypherDoctor` | Planned | Installed SDK, project, machine, and build environment | Diagnose missing dependencies, invalid configuration, stale caches, incompatible tools, and common setup failures. |

Individual visibility and lighting stages should begin as internal
`CypherMapCompiler` stages. They become separate executables only if distributed
builds or specialized debugging requires it.

## Importers And Interchange Tools

Importers are tools-side adapters, not runtime dependencies.

| Importer family | Approved direction |
| --- | --- |
| Images | libpng, libjpeg-turbo, TinyEXR, and KTX/Basis tooling behind Cypher import contracts. |
| Meshes | cgltf first, MikkTSpace and meshoptimizer in cooking, Assimp only as a fallback. |
| Audio | libsndfile for source decoding, Opus for applicable cooked streams/voice. |
| Fonts | FreeType and HarfBuzz for import, rasterization, shaping, and validation. |
| Archives | libzip for ZIP interchange; CypherPak remains the runtime package format. |
| Shaders | shaderc/glslang and SPIRV-Cross behind Cypher compiler/reflection contracts. |

Import workflows need preview, import presets, reimport, source tracking,
dependency invalidation, diagnostics, and deterministic cooking. They should not
expose third-party types in public engine formats.

## Diagnostic And Developer Tools

These capabilities may appear in Mason, a focused Qt product, a Dear ImGui
runtime overlay, or more than one view backed by the same telemetry protocol.

| Capability | Qt view | Runtime overlay | Headless/report support |
| --- | --- | --- | --- |
| Logs, channels, commands, CVars | CypherConsole/Mason | Yes | Text and structured logs |
| CPU/GPU profiling | CypherProfiler/Mason | Summary | Trace export |
| Memory and allocator state | CypherMemoryInspector | Summary | Snapshot/diff reports |
| Renderer passes/resources | CypherScope/Mason | Yes | Capture metadata and dumps |
| Physics shapes/contacts/queries | Mason Physics Lab | Yes | Deterministic test reports |
| AI perception/navigation/decisions | Mason AI workspace | Yes | Scenario test reports |
| Network packets/replication/prediction | CypherNetworkInspector | Yes | Packet/replay analysis |
| Filesystem mounts, packages, and IO | CypherScope | Yes | CypherPak, CypherDependencyInspector, and IO traces |
| Entity/component/world state | Mason/CypherScope | Yes | Scene/world dumps |
| Replay and determinism | CypherReplayInspector | Controls | CypherReplayInspector reports |
| Crash and stack diagnostics | CypherCrashViewer | No | Crash bundle and symbols |
| Shader/material inspection | Mason/CypherScope | Yes | Compiler reflection/disassembly |
| Asset dependencies and cook state | Mason/CypherScope | Limited | CypherDependencyInspector, CypherCacheManager, and build reports |

## Supporting Services

The visible tools depend on non-UI services that must be designed as reusable
libraries or processes:

- project and workspace model
- VFS and package mounting
- asset registry and stable resource IDs
- SQLite-derived asset index
- file watching and hot reload
- source control integration boundary
- reflection and schema registry
- CYKV parser, writer, validator, and migration framework
- importer and compiler registry
- dependency graph and incremental build scheduler
- derived-data cache
- thumbnail and preview service
- background job system and cancellation
- editor command, transaction, undo, redo, and recovery journal
- inter-process telemetry and command protocol
- engine/editor bridge
- play-in-editor world isolation
- diagnostics database and source navigation
- plugin discovery and version compatibility policy
- headless automation API

These are not separate products merely to increase tool count. They are the
shared foundation that prevents every editor from reimplementing the same logic.

## File And Tool Ownership

| Source family | Primary editor | Primary compiler | Runtime product |
| --- | --- | --- | --- |
| `.cymap` | Mason Map | CypherMapCompiler | `.cymap_c` |
| `.cyscene` | Mason Scene/World | CypherSceneCompiler | `.cyscene_c` |
| `.cyprefab` | Mason Entity/Prefab | CypherPrefabCompiler | Cooked prefab/entity records |
| `.cymat` | Picasso Material / Mason Material | CypherMaterialCompiler | `.cymat_c` |
| `.cytex` | Picasso Texture / Mason Texture Lab | CypherTextureCompiler | `.cytex_c` |
| model source metadata | Mason Model | CypherModelCompiler | `.cymesh_c`, `.cyskel_c` |
| animation source metadata | Mason Animation | CypherAnimationCompiler | `.cyanim_c` |
| animation graph source | Mason Animation Graph | CypherAnimationGraphCompiler | Cooked animation graph evaluator resource |
| `.cyshader` | Mason Shader | CypherShaderCompiler | `.cyshader_c` |
| VFX source | Mason VFX | CypherVFXCompiler | Cooked VFX resource |
| audio source metadata | Mason Audio | CypherAudioCompiler | `.cysnd_c` |
| audio event/mix source | Mason Audio | CypherAudioGraphCompiler | Cooked sound-event, rule-stack, and mix-graph resources |
| PostFX/color source | Mason PostFX and Color | CypherPostFXCompiler | Cooked post-process profile and optional LUT resource |
| typed generic CYKV data | Mason Data/Schema or Gameplay Data | CypherDataCompiler and CypherValidator | Schema-identified cooked data records |
| resource/preload manifest | Mason Build/Cook | CypherResourceCompiler | Validated runtime resource-set manifest |
| `.cyphys` | Mason Physics Lab | CypherPhysicsCompiler | `.cyphys_c` |
| `.cynav` | Mason Navigation | CypherNavigationCompiler | `.cynav_c` |
| `.cyflow` | Mason Objective/Flow | CypherFlowCompiler | `.cyflow_c` |
| CyGUI source | Mason UI/HUD | CypherUICompiler | Cooked UI resource |
| Lua scripts and binding metadata | Mason Script | CypherScriptCompiler | Validated source or cooked bytecode plus debug metadata |
| input/action source | Mason Input and Actions | CypherInputCompiler | Cooked action maps |
| localization source | Mason Font and Localization | CypherLocalizationCompiler and CypherCaptionCompiler | Cooked strings, captions, and locale metadata |
| gameplay data schemas/documents | Mason Gameplay Data | CypherKeyValues, CypherSchemaCompiler, and CypherValidator | Validated/cooked gameplay records |
| dialogue/choreography source | Mason Choreography | CypherAnimationCompiler, CypherCaptionCompiler, and CypherSceneCompiler | Cooked sequence resources |
| package manifest | Mason Package/Release | CypherPak | `.cypak` |
| mod/add-on manifest | Mason Mod and Add-on | CypherModManager and CypherPak | Validated mod packages and manifests |

Exact extensions for animation graphs, audio graphs, PostFX, generic data, VFX,
UI, dialogue, replay, resource manifests, subrect metadata, and intermediate
products remain decisions for their respective format designs. Extensions must
not be invented only to make the list look complete.

## Newly Identified Additions

The following capabilities were missing or insufficiently explicit in earlier
Mason lists:

| Addition | Why it is needed | Decision |
| --- | --- | --- |
| Project manager/launcher | Keeps SDK, project, target, build, and launch configuration outside ad hoc scripts. | Proposed |
| Associated-editor registry | Lets the Asset Browser resolve preview, validator, compiler, and editor ownership without hard-coded Qt dispatch. | Decided as Resource/Asset Browser responsibility |
| Resource/preload manifest tooling | Startup groups, maps, mods, servers, and tests need validated explicit resource sets and load policy. | Planned |
| Generic typed data compiler/editor | Weapons, enemies, waves, response rules, and project-defined records need schema-driven authoring without bespoke formats for every type. | Planned |
| Texture subrect and hotspot editor | Sprites, atlases, pivots, UI regions, and world-material hotspots require visual region authoring. | Planned as Texture Lab mode |
| Character setup and facial tools | Bridges model flexes, gaze, mouth, dialogue, and choreography. | Planned |
| UI/HUD authoring | CyGUI needs a visual workflow, localization preview, and input-navigation testing. | Planned |
| Font/localization/caption tools | Shipping text requires glyph, shaping, fallback, subtitle, and locale validation. | Planned |
| Shader and permutation inspector | Renderer development requires reflection, compile diagnostics, disassembly, and variant control. | Planned |
| PostFX/color-grading tools | Camera and atmosphere work need reproducible visual profiles and volume authoring. | Planned |
| Animation graph compiler/debugger | State, blend, IK, event, authority, and parameter behavior needs deterministic compilation and live inspection. | Decided |
| Audio event and mix graph tools | Raw samples alone cannot represent sound events, routing, rule stacks, buses, and runtime mixes. | Planned |
| Procedural object rules | Parameterized reusable objects can reduce repetitive world work after the prefab system is proven. | Deferred |
| Response-rule editor | Conditional dialogue and reaction selection need schema validation and simulation when game AI requires them. | Deferred |
| Terrain/foliage tools | Larger outdoor or mixed maps require specialized world-authoring workflows. | Planned, demand-driven |
| Replay/determinism inspector | Multiplayer, prediction, testing, and debugging require timeline and state comparison. | Planned |
| Network inspector and simulator | Multiplayer needs packet, replication, loss, latency, and prediction diagnostics. | Planned |
| Memory inspector | Long-running engine/editor sessions need allocation ownership and fragmentation visibility. | Planned |
| Crash viewer | Cross-platform crash bundles must remain inspectable when the main process cannot run. | Planned |
| Dependency/reference repair | Renaming and moving assets safely requires graph-aware tooling. | Decided as Asset Browser responsibility |
| Schema migration tools | Proprietary formats require explicit, testable evolution. | Planned |
| Package/release manager | Shipping builds require closure checks, signing, patching, and release manifests. | Planned |
| Script editor/debugger | Lua gameplay requires binding inspection, reload, breakpoints, stack views, and deterministic diagnostics. | Decided |
| Input/action editor | Cross-platform keyboard, mouse, controller, and accessibility mappings require conflict-aware authoring. | Planned |
| Gameplay data editor | Schema-defined weapons, enemies, waves, and game modes should not require bespoke UI for every field. | Planned |
| Mod/add-on manager | The moddability goal requires manifests, dependencies, package validation, installation, and publishing workflows. | Planned |
| Dedicated-server console | Multiplayer development and operation require remote administration and telemetry outside the game window. | Planned |
| Symbol processor | Crash reports and profiler captures require reproducible build-ID and symbol resolution. | Planned |
| Benchmark viewer | Performance work needs baseline history and variance-aware comparison, not isolated terminal numbers. | Proposed |
| Distributed build monitor | Large lighting/shader/cook workloads may eventually need workers and observability. | Deferred |
| Visual regression/test lab | Renderer, map, physics, UI, and asset workflows benefit from reproducible comparison tests. | Proposed |

## Implementation Order

### Stage 0: Current Foundations

- finish required CypherCommon contracts
- finish security, jobs, resource handles, parsing, serialization, reflection,
  filesystem, packages, diagnostics, and math prerequisites
- maintain tests, sanitizers, benchmarks, and cross-platform CI

Exit condition: tools can share stable low-level APIs without owning runtime
implementation details.

### Stage 1: Data Language And Schemas

- CYKV lexer, parser, writer, formatter, and diagnostics
- schema validation and reflection bridge
- stable IDs, source locations, migrations, and deterministic round trips
- initial CypherKeyValues, CypherSchemaCompiler, CypherValidator,
  CypherFormatInspector, and CypherDataDiff tools

Exit condition: source documents can be authored and validated without a GUI.

### Stage 2: Resource Pipeline

- asset registry and dependency graph
- associated-editor, preview-provider, validator, and compiler registry
- importer/compiler registry
- derived-data cache
- CypherResourceCompiler coordinator
- resource/preload manifests and generic typed data cooking
- texture, mesh, material, and shader vertical slices
- CypherScope first useful version

Exit condition: a source asset can be imported, cooked, inspected, loaded,
reloaded, and diagnosed end to end.

### Stage 3: Runtime World And Map Compiler

- minimal runtime world
- `.cymap` schema and typed document
- CypherMapCompiler geometry/entity/collision path
- cooked world loader
- command-line playable test room

Exit condition: a map can be built and played without Mason.

### Stage 4: Mason Foundation

- Qt 6 application shell
- project/document/workspace model
- engine viewport
- hierarchy, inspector, asset browser, console, diagnostics
- selection, transforms, undo/redo, save, compile, and play

Exit condition: Mason can edit, compile, and play the command-line test room.

### Stage 5: Production World Tools

- complete map editing
- entities, prefabs, materials, lighting, navigation, physics, layers, and flow
- incremental builds and hot reload
- package/release path for the first complete arena

### Stage 6: Content Workspaces

- model, animation, and animation graphs
- material, texture, and shader
- VFX
- audio samples, events, rule stacks, and mix graphs
- PostFX and color authoring
- UI/HUD, fonts, localization, and captions
- scripts, input actions, gameplay data, and settings
- AI behavior and navigation

Each workspace starts only after its runtime and headless compiler path exists.

### Stage 7: Advanced Production And Diagnostics

- choreography, facial animation, and dialogue
- cinematic sequencing
- replay, network, memory, and crash inspection
- post-processing and color grading
- package publishing and patch generation
- mod/add-on packaging and dedicated-server administration
- optional distributed build workers

## Acceptance Criteria For Every Tool

An interactive editor is not complete merely because its window opens. Each
tool must eventually provide:

- clear ownership and lifecycle
- undo/redo for persistent edits
- deterministic save and compile behavior
- source and schema version handling
- actionable diagnostics with source/object locations
- crash recovery for editable documents
- cancellation and progress for long operations
- keyboard and mouse accessibility
- high-DPI support
- Windows, Linux, and macOS validation
- representative tests for document and command behavior
- compiler golden tests where applicable
- performance tests for large representative data
- automation through the same headless libraries or CLIs
- no dependency on global editor state for compiler correctness

## Scope Boundary

The complete list is a destination, not the next milestone.

CypherEngine should not build twenty empty Qt executables. It should first build
shared data contracts and one useful vertical slice. Mason then exposes mature
capabilities as workspaces. A focused standalone executable is created only when
crash isolation, remote use, startup cost, or a specialized workflow justifies
it.

The immediate priority is preserving the completed CYKV/render-asset compiler
slice, enforcing Common/runtime/tool build boundaries, and defining the first
runtime resource/renderer consumer. Standalone Qt texture and material shells
follow the reusable preview service; they do not precede the runtime.
