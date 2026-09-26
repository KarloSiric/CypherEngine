<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/future_implementations.md
//  Purpose: The living backlog of development infrastructure and engine,
//           editor, and tooling work still to be built, with the research
//           and audit behind each priority.
//  Details: Evidence labels: VERIFIED (read in this repository or in the
//           cited source), INFERENCE (reasoned from evidence), PROPOSAL (a
//           design not yet accepted). Items leave this file when they gain
//           an ADR or an implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Future Implementations

## 1. Where the engine stands (audit, 2026-09-26)

VERIFIED by counting the source tree (`CY_ASSERT`/`CY_VERIFY`, `CYPHER_LOG*`/
`Cy_LogWrite`/`CY_LOG_WRITE`, and `Cy_Profile*` occurrences):

| Module | Lines | Asserts | Log calls | Profile zones |
| --- | ---: | ---: | ---: | ---: |
| CypherCommon/Tier0 | 15,395 | 8 | (definitions) | (definitions) |
| CypherCommon/Tier1 | 62,373 | 997 | 0 | 0 |
| CypherCommon/Mathlib | 19,981 | 304 | 0 | 0 |
| CypherCommon/Security | 4,705 | 118 | 0 | 0 |
| CypherCommon/Tier2, Formats, ToolFramework | 27,658 | 0 | 0 | 0 |
| CypherEditor/Geometry | 87,645 | 7 | 0 | 0 |
| CypherEditor/Core (new) | 3,294 | 6 | 3 | 0 |
| CypherTools (TileEditor, Picasso) | 36,899 | 0 | 0 | 0 |
| CypherRender | 7,258 | 0 | 11 | 0 |
| CypherFileSystem, Memory, System, Pak, Engine, Resource | 23,705 | 0 | 2 | 0 |
| CypherLog, CVar, Config, Command | 3,585 | 0 | (definitions) | 0 |

What already exists (VERIFIED):

- **Build configurations** (`CMakeLists.txt`): `Debug` → `CYPHER_CONFIG_DEBUG`,
  `RelWithDebInfo` → `CYPHER_CONFIG_DEVELOPMENT`, `Release` →
  `CYPHER_CONFIG_RELEASE`, `MinSizeRel`/`Shipping` → `CYPHER_CONFIG_SHIPPING`.
- **Asserts** (`CypherCommon_Assert.h`): `CY_ASSERT`, `CY_ASSERT_MSG`,
  `CY_VERIFY`, `CY_VERIFY_MSG`, a replaceable process-wide handler; enabled in
  Debug and Development, compiled out elsewhere (VERIFY still evaluates).
- **Tier0 logging** (`CypherCommon_Log.h`): levels Trace..Fatal, 30 channels
  (Editor, Render, FileSystem, ...), per-channel toggles, one replaceable
  callback; without a callback records go to stderr.
- **CypherLog** module: `CYPHER_LOG_TRACE/DEBUG/INFO/WARNING/ERROR/FATAL`
  printf-style macros, configuration, several output formats.
- **Tier0 profile events** (`CypherCommon_Profile.h`): zone begin/end tokens,
  counters, frame begin/end, delivered synchronously to one sink. Used at five
  tool call sites; no scoped macro, no capture, no viewer.
- **Tier0 diagnostics**: crash handler, minidumps, stack traces, memory
  tracker and debug allocator, validator, CPU detection and monitoring,
  performance counters, stats.
- **CVars** (648 lines): string-keyed registry with ARCHIVE, READONLY, CHEAT,
  and DEV flags. **Commands** (475 lines). **Config** `.cfg` execution.
- **Placeholders only**: `CypherTools/CypherProfiler`, `CypherConsole`,
  `CypherMemoryInspector` contain a `.gitkeep`.

Gaps (INFERENCE from the above):

1. Asserts live almost entirely in the foundation libraries; runtime modules,
   the editor, and the tools have next to none - including recent geometry and
   editor code, which relied on status codes and tests instead.
2. Logging is effectively unused outside the renderer, and Tier0 records are
   not bridged into CypherLog, so they never reach log files or a console.
3. The profiler is an event contract with no zones in engine code, no capture,
   and nothing to look at.
4. The console stack (cvars, commands, config) is small and has no console UI.

## 2. What other engines did (research)

**Source** (Source SDK 2013 public headers, study-only under
[reference_policy.md](reference_policy.md)):

- VERIFIED `tier0/vprof.h`: scoped `VPROF( name )` and
  `VPROF_BUDGET( name, group )` build a per-frame call tree of nodes (calls,
  time, peak); about 40 named *budget groups* ("World Rendering", "Physics",
  "Game", "Networking", "Sound", ...) attribute time to systems; counters via
  `VPROF_INCREMENT_COUNTER`; reports sorted by total, average, peak, and peak
  over average; everything compiles to nothing when VProf is disabled.
- VERIFIED [Showbudget](https://developer.valvesoftware.com/wiki/Showbudget):
  `+showbudget` draws ms-per-frame bars per budget group over an FPS graph;
  requires cheats; the page warns it does not account for multithreading
  correctly - a lesson for our design.
- VERIFIED `tier0/dbg.h`: `Msg`, `Warning`, `Error`, `DevMsg( level, ... )`
  and `DevWarning` gated by a developer level, grouped spew with
  `SpewActivate( group, level )`, one replaceable `SpewOutputFunc`, and
  `Assert`, `AssertMsg`, `AssertOnce`, `AssertFatal` gated by `DBGFLAG_ASSERT`.
- VERIFIED `tier1/iconvar.h`: `FCVAR_DEVELOPMENTONLY` (hidden in released
  products), `FCVAR_HIDDEN`, `FCVAR_CHEAT`, `FCVAR_REPLICATED`,
  `FCVAR_PROTECTED`, `FCVAR_DEMO`, `FCVAR_RELOAD_MATERIALS`,
  `FCVAR_NOT_CONNECTED`, and more.

**CryEngine**:

- VERIFIED (file names in the mirror cited by
  [cryengine1_subsystem_research.md](cryengine1_subsystem_research.md)):
  CE1 `CrySystem` holds lifecycle (`System*`), console and cvars
  (`XConsole*`), `Log`, the frame profiler with an on-screen renderer
  (`FrameProfileSystem`, `FrameProfileRender`), timers, CPU detection, crash
  call stacks, memory accounting (`CryMemoryManager`, `CrySizer*`), the pak
  file system (`CryPak`, `ZipDir*`), streaming, XML, Lua script bindings
  (`ScriptObject*`), an in-process Lua debugger, and vendored zlib and Expat.
- VERIFIED CRYENGINE 5
  [Profiling and Debugging](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/28184591):
  `r_Profiler 1/2` shows main thread, render thread, and GPU against a target
  frame rate; a boot profiler for startup and level loads; MemReplay for memory;
  an external CPU profiler (Brofiler) for detail; `p_draw_helpers` and the
  auxiliary-geometry and persistent-debug interfaces for debug drawing;
  `sys_perfhud` as a UI panel over these tools.

**Tracy** (VERIFIED [README](https://github.com/wolfpld/tracy), BSD-3):
an instrumented client streams nanosecond-resolution telemetry to a standalone
viewer; covers CPU zones, GPU (OpenGL, Vulkan, D3D, Metal), memory, locks,
context switches, and frame screenshots.

**CrySystem size versus ours** (INFERENCE from file sizes in the mirror): of
CrySystem's roughly 60k lines, about 20k are vendored zlib and Expat, about 11k
are Lua script bindings, 6k the pak/zip system, and 2-2.5k each the profiler,
the console with cvars, and the Lua debugger. CypherEngine splits the same
responsibilities across `CypherSystem` (4.2k), `CypherFileSystem` (6.5k),
`CypherPak` (3k), `CypherMemory` (5k), `CypherLog`, `CypherCVar`,
`CypherCommand`, `CypherConfig`, `CypherEngine` (host), and the Tier0
diagnostics - together already comparable in size. `CypherSystem` looks small
because it is only the platform layer. The real weak points are the console
stack, the profiler, logging use, streaming, and scripting - not the line count
of one folder.

## 3. Build configurations (PROPOSAL)

| Feature | Debug | Development | Release | Shipping |
| --- | --- | --- | --- | --- |
| Optimisation | off | on | on | on (+LTO) |
| `CY_ASSERT` | on | on | off | off |
| Log levels compiled in | all | Debug and up | Info and up | Warning and up |
| Profiler zones | on | on | on, off by default | compiled out |
| Developer console, dev cvars and commands | on | on | on (developer mode) | hidden |
| Cheats | allowed | allowed | server policy | server policy |
| Memory tracking, debug allocator | on | opt-in | off | off |
| Symbols | full | full | separate | separate, symbol server |

Release is the "fast but still inspectable" build; Shipping is what players get.

## 4. Asserts and logs (PROPOSAL, applies to all new code now)

- **Assert programmer mistakes, never data.** `CY_ASSERT` for invariants that
  only a bug can break (index in range after validation, sorted tables, state
  machines, "cannot fail after reserve"). User files, network input, and
  anything a person can type are validated and return a status.
- **Log events someone must see, once, at the boundary.** Libraries return
  statuses; the caller that decides what the status means logs it (the
  settings file policy logging a restored backup is the model). Levels: Error =
  something failed and the user is affected; Warning = recovered, but look;
  Info = lifecycle (startup, map load, save); Debug/Trace = developer detail.
- **Every log names its channel** (`log_channel_t`), so the console can filter
  by system and developers can toggle channels at runtime.
- **Retrofit order:** runtime modules first (FileSystem, Pak, Resource, Engine
  host, Render, Memory, System), then Geometry command paths, then the tools.
- **Bridge Tier0 into CypherLog** so every record reaches log files, the
  in-game console, and the editor console with one formatting path.

## 5. The profiler (PROPOSAL)

**Goal:** mark code with `CY_PROFILE_ZONE( "Section 1" )` and see, per frame
and per thread, how long every zone and everything under it took - live in the
engine and editor, and afterwards in a capture.

**Instrumentation API** (PROPOSED names):

```cpp
CY_PROFILE_FRAME();                          // once per frame, per frame loop
CY_PROFILE_ZONE( "Physics Step" );           // scoped: ends at closing brace
CY_PROFILE_ZONE_GROUP( "Cull", Render );     // attributes time to a budget group
CY_PROFILE_FUNCTION();                       // zone named after the function
CY_PROFILE_COUNTER( "Draw calls", n );       // per-frame value
CY_PROFILE_THREAD_NAME( "Render" );
```

**Design:**

- Zones are static descriptors (name, group, source location) created once per
  call site; an event is only `{ descriptor, begin tick, end tick, depth }`.
- Each thread writes into its own lock-free ring buffer - no global lock on the
  hot path. A collector drains buffers each frame, builds the per-frame zone
  tree (like VProf), keeps budget-group totals, and retains the last N frames
  of raw timeline for spike inspection.
- Threads are first-class (the Showbudget lesson): per-thread timelines, and
  budget groups summed per thread, not blended.
- Disabled cost is one predictable branch per zone; Shipping compiles zones out.
- Built on the existing Tier0 profile event contract, replacing its
  synchronous sink with the buffered path.
- GPU zones later through backend timer queries (OpenGL first).

**Views:**

| View | What it shows |
| --- | --- |
| Console reports | `profile.report hierarchy|flat|peak`, `profile.spikes <ms>` |
| In-game overlay | frame-time graph plus per-group budget bars; `profile.overlay 0/1/2` |
| Editor panel | timeline per thread, flame graph, zone table (count, total, self, average, peak), frame scrubber |
| `CypherProfiler` tool | standalone Qt viewer that connects live over a socket or opens a `.cyprofile` capture |
| Export | Chrome trace JSON; optional Tracy backend behind the same macros |

**Console commands** (PROPOSED): `profile.on`, `profile.off`,
`profile.capture <frames> <file>`, `profile.report`, `profile.spikes`,
`profile.overlay`, `profile.filter <group>`, `profile.threads`.

**Milestones:** P1 zones, per-thread buffers, collector, console report. P2
overlay and budget groups. P3 capture files and the standalone viewer. P4 the
editor panel. P5 GPU zones, memory and lock events, Tracy export.

**Finding bottlenecks, not just timing** (PROPOSAL, added 2026-09-26):

- Every zone reports calls, total time, *self* time (total minus children),
  average, minimum, maximum, and standard deviation per frame, so the table
  sorted by self time is a hotspot list.
- Per-zone history graphs: pick a zone and see its cost over the last N
  frames; spikes are marked and clickable into the timeline of that frame.
- Spike capture: `profile.spikes <ms>` keeps the full timeline of any frame
  slower than the threshold, so a one-in-a-thousand hitch is not lost.
- Capture comparison: load two captures and diff zones (before/after an
  optimisation).
- Instrumentation only sees marked code. To find cost in functions nobody
  marked, add a **sampling mode** (P6): periodic call-stack samples of every
  thread, resolved to function names with symbols, shown as a flame graph
  beside the instrumented zones. Compiler-inserted per-function hooks are an
  alternative for short focused sessions but slow everything down.
- Zones also record the frame phase (input, simulation, render submit, GPU,
  present) so the overlay can say which phase is over budget.

## 6. Console, developer mode, cvars, and commands (PROPOSAL)

- **One console model** (`EditorCore/Console` in the editor, the same model in
  the game): log view with channel and level filters and colours, history,
  completion over commands and cvars, and output from both engine commands and
  editor commands.
- **Developer mode:** a `developer` level (0 off, 1 messages, 2 verbose) gates
  developer messages at runtime, as Source's `DevMsg` does; dev-only cvars and
  commands are hidden unless developer mode is on, and compiled out of
  Shipping.
- **Scaling to thousands of cvars and commands:** registration by static
  module tables (like the editor command registry); mandatory help text and
  default; prefixes per system (`r_`, `fs_`, `snd_`, `net_`, `sv_`, `cl_`,
  `phys_`, `ai_`, `ed_`, `profile_`); flags to add beside the current four:
  HIDDEN, REPLICATED, SERVER, CLIENT, NOTIFY, RESTART_REQUIRED, DEMO,
  RELOAD_RESOURCES; change callbacks; `find`, `help`, `cvarlist`, `differences`
  (non-default values) commands; a test that fails when a cvar lacks help.
- **Editor and engine consoles** share one window: editor commands
  (`module.command`) and engine console commands/cvars both run from it.

## 7. Debugging and diagnostics backlog

- Debug drawing: per-frame auxiliary geometry and persistent timed helpers.
- Overlays: FPS and frame time, memory, draw calls, streaming, network graph.
- Memory: per-system budgets, leak reports at shutdown, a memory inspector
  (`CypherMemoryInspector`).
- Crash reporting: minidumps plus a symbol server (`CypherSymbolManager`).
- Load-time and boot profiling.
- GPU: timer queries, RenderDoc capture command.
- Hot reload of shaders, materials, scripts, config, and maps.
- Replay and demo recording (`CypherReplayInspector`).
- Automated performance benchmarks with regression tracking
  (`CypherBenchmarkRunner`), fuzzing of every file parser.
- Assert UX: "ignore once / ignore always / break", assert counters in reports.
- A perf HUD panel collecting these tools for controllers and consoles.

## 8. Engine subsystems to build or strengthen

VERIFIED empty modules (0 lines): CypherAI, CypherAnimation, CypherAudio,
CypherClient, CypherEntity, CypherFont, CypherGame, CypherInput,
CypherNetwork, CypherPhysics, CypherScript, CypherServer, CypherUI.
`CypherWorld` has 180 lines; `CypherRender` 7.3k (OpenGL backend, far from
done).

Priority (INFERENCE): console stack and logging bridge → profiler P1-P2 →
Input → World/3D engine and render → Entity → Physics → Audio → Script →
Network → Animation and AI → UI and Font. Streaming belongs with World and
Resource.

## 9. Source tree consolidation (PROPOSAL, pending decision)

Several top-level folders hold too little to justify themselves (VERIFIED):
`CypherCommand` 3 files / 475 lines, `CypherCVar` 3 / 648, `CypherConfig`
3 / 579, `CypherLog` 6 / 1,883, `CypherWorld` 4 / 180, `CypherSystem`
12 / 4,227, `CypherPak` 10 / 2,961.

Why CryEngine 1 looked bigger (VERIFIED from
[cryengine1_subsystem_research.md](cryengine1_subsystem_research.md)):
its modules were years of shipped Far Cry code, its files were large (CryInput
20 files / 9,349 lines, about 470 lines per file), much of the count was
bundled third-party code (FreeType is 314 of CryFont's 334 files), and
CrySystem absorbed many services. Small modules existed there too (CryInput,
CryEntitySystem, CryFont engine code, CryScriptSystem engine code: 17-20
files each). Our folders are small because the services are young and thin,
not because splitting by service is wrong.

Proposed direction, keeping ADR 0006's rule that every subsystem has its own
CMake target and ADR 0003's short prefixes:

```text
src/CypherSystem/
    Platform/   today's CypherSystem (Sys_, GLimp_)        Cypher::System
    Memory/     today's CypherMemory (Mem_)                Cypher::Memory
    Log/        today's CypherLog (Log_) + Tier0 bridge    Cypher::Log
    Console/    CypherCommand + CypherCVar + CypherConfig  Cypher::Console
                (Cmd_, Cbuf_, Cvar_, Cfg_) + console model
    Profiler/   capture, aggregation, reports, export      Cypher::Profiler
src/CypherFileSystem/
    (root)      today's VFS (FS_)                          Cypher::FileSystem
    Pak/        today's CypherPak (Pak_)                   Cypher::Pak
```

Future large modules stay top-level with internal subfolders, e.g.
`CypherAudio/{Mixer, Spatial, Streaming, Backends}` (spatial 3D audio inside
Audio, as its own target only if it earns one), `CypherRender/{Frontend,
OpenGL, Vulkan}`, `CypherNetwork/{Transport, Channel, Replication}` with the
`NET_`/`Netchan_` prefixes. Empty placeholder folders stay as they are.

## 10. Editor backlog

- `Gui/` framework extracted from TileEditor (theme, settings dialog,
  console), Qt Advanced Docking System (dependency download needs approval).
- Mason shell in `App/Mason`; built-in `charcoal.cytheme` from TileEditor's
  colours; migration of TileEditor's INI themes and `editor.ini`.
- `Core/`: Console model, Vfs mount profiles, Assets and DataBase View,
  Notifications, Undo, Selection, Personalization, Scripting (Python plugins).
- The full map: `cypher.map` V10 schema and chunk layout; map workspace.
- Workspaces over the network (browse, copy, pull maps from peers) and MCP
  automation, both through the command registry.
- Content editors: materials, shaders (live editing), particles, models,
  entities and prefabs, physics, sequences.
- Play mode launching the game with the current map.
