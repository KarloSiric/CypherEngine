<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/full_engine_plan.md
//  Purpose: The long-range plan for the whole engine, editor, and tooling:
//           tracks, phases with exit criteria, per-subsystem plans, the
//           renderer backend strategy, and the decisions still open.
//  Details: Near-term execution detail lives in six_month_engine_plan.md and
//           the development-infrastructure backlog in
//           future_implementations.md; this document sets the order they sit
//           in. Phases are gates, not dates.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Full Engine Plan

## 1. How this plan fits with the others

| Document | Role |
| --- | --- |
| This plan | Long-range order: tracks, phases, subsystem plans, open decisions |
| [six_month_engine_plan.md](six_month_engine_plan.md) | Near-term execution of the runtime critical path (renderer R1-R6, graybox), with monthly gates |
| [future_implementations.md](future_implementations.md) | Development-infrastructure design and backlog: profiler, console, logs, asserts, debugging tools |
| [current_status.md](current_status.md) | What is active right now |
| [master_plan.md](master_plan.md) | The original June 2026 plan; its dated schedule is historical |

**Priority change recorded here (2026-09-25):** the six-month plan treated
editor work as off the critical path. The project owner has since made the
editor frontend a parallel track of its own. The runtime critical path keeps
its order; the editor, development infrastructure, and tools run beside it
without taking over renderer decisions.

## 2. Where we are (2026-09-27)

| Area | State |
| --- | --- |
| Foundation (CypherCommon) | ~139k lines: tiers 0-2, math, formats, security, tool framework. Strong. |
| System layer (CypherSystem) | Platform, Memory, Log, Console (Command/CVar/Config) - implemented, thinly used (ADR 0010). |
| File system and resources | VFS, Pak, resource ownership, deterministic cooking of shaders, textures, materials. |
| Renderer | 7k lines: lifecycle, OpenGL bootstrap, buffers, vertex input. No program, pipeline, or draw yet. |
| World | 180 lines of contracts. |
| Input, Entity, Physics, Audio, Network, Script, Animation, AI, UI, Font, Game, Client, Server | Empty folders. |
| Editor | Geometry library complete (87.6k lines, flattened); EditorCore started (commands, keymaps, themes, layouts, workspaces, settings files); formats specified (ADR 0009). |
| Tools | TileEditor, Picasso, resource/shader/texture/material compilers; profiler, console, and memory-inspector tools are placeholders. |

Scale reference from CryEngine 1 (Crytek-owned code, from
[cryengine1_subsystem_research.md](cryengine1_subsystem_research.md)): renderer
198k lines across three graphics APIs, game 93k, 3D engine 44k, animation 35k,
physics 32k, AI 22k, network 16k, entity 12k, sound 9k, input 9k. Most of that
volume sits in subsystems that are still empty here.

## 3. Principles

1. **One complete path before breadth.** Every phase ends with something that
   runs end to end in the real executable, not with more scaffolding.
2. **Playable loop first, movement feel before content scale** (CLAUDE.md).
   REAP's arena loop is the proof that the engine works.
3. **Our own core systems.** Physics and the entity system are written in
   house (owner decision, 2026-09-27). Third-party code stays where it is not
   the product: platform (SDL3), compression, image decoding, shader
   compilation, font rasterisation if needed.
4. **Two backends before three.** A renderer abstraction is only proven when a
   second backend runs the same frames; no third backend starts before that.
5. **Every feature ships with** tests, asserts on its invariants, logs at its
   boundaries, profiler zones on its hot paths, and a console command or cvar
   when it has runtime state worth inspecting.
6. **Gates, not dates.** A phase that misses its exit criteria continues; later
   scope moves out rather than stacking on an unstable base.

## 4. Tracks

Work runs in four tracks. Ownership follows the six-month plan's operating
model: the project owner leads the renderer; agents take the parallel tracks
with explicit file ownership.

| Track | Lead | Contents |
| --- | --- | --- |
| **A. Runtime** | Project owner (renderer), then shared | Render, World, Input, Entity, Physics, Audio, Game loop, then Script, Animation, AI, Network |
| **B. Development infrastructure** | Agents | Log bridge, console model and UI, cvars/commands at scale, profiler, debug draw, overlays, crash reporting, CI |
| **C. Editor and tools** | Agents with owner review | EditorGui, Mason shell, map format and compiler, content editors, Python plugins, networked workspaces, MCP |
| **D. Content pipeline** | Shared | Mesh, map, texture, material, audio, animation, navigation cooking; hot reload |

## 5. Phases

### Phase 1 - First Light

The six-month plan's months 1-3.

- **A:** cooked shader programs, immutable pipeline, draw, first cube in the
  Host loop, reflection, cooked material binding (R1-R5).
- **B:** Tier0 log records bridged into CypherLog; console model (history,
  completion, filters) shared by game and editor; developer mode and dev-only
  cvars; profiler P1 (zones, per-thread buffers, collector, console reports).
- **C:** EditorGui framework extracted from TileEditor (theme, settings
  dialog, console); Mason shell in `App/Mason` with the TileEditor look,
  menus and sidebar generated from commands, saved layouts; TileEditor moved
  onto the framework.
- **Exit:** a textured cube renders in `CypherEngine`; logs and profiler
  reports appear in a console; Mason opens with themes, keymaps, layouts, and
  settings that persist.

### Phase 2 - Graybox

The six-month plan's months 4-6.

- **A:** mesh resources, renderer-neutral World submission, Input actions
  (`.cyinput`/`.cybindings`), camera, a fully resident cooked static graybox,
  first collision queries (ray, sweep, overlap) against the static world.
- **B:** profiler P2 (overlay, budget groups); debug draw; frame-time and
  memory overlays.
- **C:** `cypher.map` V10 schema and chunk layout; Mason map workspace with
  brush and mesh editing on the Geometry library; cook to the runtime map;
  CypherSceneCompiler first slice.
- **D:** map and mesh cooking; hot reload of shaders and materials.
- **Exit:** a map authored in Mason loads in the engine and a camera can walk
  through it with collision.

### Phase 3 - Playable

- **A:** own physics - character controller tuned for fast arena movement,
  rigid bodies, triggers; own entity system - identities, components,
  definitions per game profile, entity I/O; first audio (mixer, positional
  sounds); the REAP combat loop (one arena, waves, weapons).
- **B:** profiler P3 (capture files, standalone viewer); crash reports with
  minidumps and symbols; assert UX.
- **C:** entity placement and properties in Mason; entity I/O editor; Play
  mode launching the game on the current map.
- **Exit:** REAP's arena is playable end to end from an editor-authored map.

### Phase 4 - Second Backend And Scale

- **A:** Vulkan backend running the same frames as OpenGL; lighting, shadows,
  post-processing; streaming; CypherScript (VM and game scripting);
  animation; AI and navigation; spatial audio.
- **B:** profiler P4-P6 (editor panel, GPU zones, sampling mode); memory
  budgets and leak reports; benchmark regression tracking.
- **C:** material, shader (live editing), particle, model, and sequence
  editors; DataBase View; Python plugins.
- **Exit:** the arena runs on OpenGL and Vulkan with lighting and animated,
  scripted enemies.

### Phase 5 - Network And Production

- **A:** networking per ADR 0001 (coop-first listen server), replication,
  dedicated server; the DirectX 12 and software-renderer decisions (section 7).
- **B:** Shipping configuration, packaging, release symbols.
- **C:** networked workspaces (browse, copy, pull maps from peers), MCP
  automation, collaboration features.
- **Exit:** a packaged build plays coop over a network; developers share
  work between workspaces.

## 6. Subsystem plans

| Subsystem | Folder | Our approach | First slice | Later | Scale reference (CE1) |
| --- | --- | --- | --- | --- | --- |
| Renderer | `CypherRender/{Frontend, OpenGL, Vulkan, ...}` | backend table behind a renderer-neutral frontend (existing ABI) | program, pipeline, draw, cube | materials, lighting, shadows, post, GPU timers, more backends | 198k |
| World | `CypherWorld` | map state, spatial proxies, visibility, submission (ADR 0004/0006) | one world, handles, frustum query, submission | streaming, portals/PVS, terrain | 44k |
| Input | `CypherInput` | actions and contexts over System events | frame-state reducer (NR-05), action map | rebinding UI, gamepads | 9k |
| Entity | `CypherEntity` | own, data-oriented: generation handles, sparse-set components, game-profile definitions | identities, transforms, component storage | entity I/O, triggers, prefabs, replication hooks | 12k |
| Physics | `CypherPhysics` | own: collision model from cooked geometry, queries, then dynamics | static collision queries, character controller | rigid bodies, constraints, ragdolls, determinism for networking | 32k |
| Audio | `CypherAudio/{Mixer, Spatial, Streaming, Backends}` | own mixer and streaming over a device backend; spatial audio (HRTF, occlusion, reverb, propagation) through Valve's Steam Audio SDK behind `CypherAudio/Spatial` (owner decision 2026-09-29; adding the SDK is a reviewed dependency step) | device, mixer, positional one-shots | streaming music, Steam Audio occlusion, reverb zones, HRTF | 9k |
| Script | `CypherScript` | own VM and game scripting (development phase 12) | VM core, bindings to entities | hot reload, debugger | 5k engine + Lua |
| Animation | `CypherAnimation` | skeletal animation, blending | clip playback | blend trees, IK | 35k |
| AI | `CypherAI` | navigation from cooked navigation data, behaviour | path queries | behaviour trees, perception | 22k |
| Network | `CypherNetwork` (`NET_`, `Netchan_`) | coop-first listen server (ADR 0001) | transport, channel | replication, prediction, dedicated server | 16k |
| UI / Font | `CypherUI`, `CypherFont` | renderer-neutral draw lists (ADR 0006) | text measurement and glyph atlas; in-game developer console in the GoldSrc/Source tradition (drop-down, monospace, history, completion) | HUD, menus | 5k font |
| Game | `CypherGame`, `CypherClient`, `CypherServer` | REAP rules on engine services | movement, one weapon, one enemy | waves, arena flow | 93k |

## 7. Renderer backend strategy

| Backend | Status | When | Gate |
| --- | --- | --- | --- |
| OpenGL | active | Phases 1-3 | the reference backend; every renderer feature lands here first |
| Null | small | Phase 1 | headless tests and servers need a renderer that draws nothing |
| Vulkan | planned | Phase 4 | starts once the OpenGL path renders the Phase 3 arena; must match its frames |
| DirectX 12 | undecided | Phase 5 decision | only if a target platform needs it (Windows-only; Xbox); needs a named requirement |
| Software | undecided | Phase 5 decision | valuable as a reference renderer for image tests and GPU-less machines; a large project on its own, so it waits for a concrete use |

Rules: backends implement the same frontend contract; backend-specific code
never leaks upward; each new backend is proven by running the same captured
frames and comparing images.

## 8. Development infrastructure order

From [future_implementations.md](future_implementations.md):

1. Log bridge and log policy retrofit (runtime modules first).
2. Console model and in-game/editor console presentation; developer mode.
3. Profiler P1 (zones, buffers, reports) → P2 (overlay, groups) → P3
   (captures, viewer) → P4 (editor panel) → P5 (GPU, memory, locks) → P6
   (sampling).
4. Debug draw and overlays.
5. Crash reports and symbols; memory budgets; benchmark regression tracking.
6. Cvar/command scale-out as subsystems land (help text, flags, find/dump).

## 9. Editor order

1. EditorGui framework and Mason shell (Phase 1).
2. Qt Advanced Docking System dependency (needs approval to download).
3. Built-in charcoal theme and default keymap; TileEditor INI migration.
4. EditorCore: console, VFS mount profiles, assets and DataBase View,
   notifications, undo, selection, personalization.
5. Map workspace on `cypher.map` V10 (Phase 2).
6. Entities, I/O, Play mode (Phase 3).
7. Content editors, Python plugins (Phase 4).
8. Networked workspaces and MCP (Phase 5).

## 10. Open decisions

| Decision | Options | Needed by |
| --- | --- | --- |
| Audio device backend | OpenAL Soft (already a vcpkg feature) vs SDL3 audio vs platform APIs | Phase 3 |
| Physics determinism | bit-exact fixed step (helps networking and replays) vs tolerant | Phase 3, before networking |
| Entity model | pure sparse-set components vs archetype tables | Phase 3 first slice |
| Script language | own language on own VM vs existing language on own VM | Phase 4 |
| DirectX 12 | build or skip | Phase 5 |
| Software renderer | reference/CI renderer vs full fallback vs skip | Phase 5 |
| Qt Advanced Docking System | adopt vs own docking | Phase 1 (editor) |

## 11. Risks

- **Breadth over integration** - many started systems, none finished. Response:
  phase exit criteria are end-to-end and checked before new scope.
- **Renderer bottleneck** - one owner on the critical path. Response: parallel
  tracks prepare tests, fixtures, and tools against stable contracts only.
- **Own physics and entity** - large, subtle systems. Response: start with the
  narrow slice the arena needs (static queries, character controller), measure
  movement feel early, grow from real gameplay needs.
- **Editor scope** - the editor can absorb unlimited effort. Response: each
  editor step is tied to a runtime phase it serves.
