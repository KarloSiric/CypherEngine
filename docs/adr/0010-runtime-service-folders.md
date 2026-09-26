# ADR 0010: Runtime Service Folders

**Status:** Accepted
**Date:** 2026-09-27
**Supersedes:** the folder layout of the runtime services in
[ADR 0006](0006-runtime-subsystem-structure.md). ADR 0006's rules stand: every
subsystem has its own CMake target, lifecycle belongs to `CypherHost`, and
features stay with a coherent owner.

## Context

Five small runtime services each had a top-level folder: `CypherCommand`
(3 files, 475 lines), `CypherCVar` (3, 648), `CypherConfig` (3, 579),
`CypherLog` (6, 1,883), and `CypherMemory` (14, 4,966), beside `CypherSystem`
(12, 4,227) and `CypherPak` (10, 2,961). The top level read as a list of tiny
modules rather than as the engine's major subsystems.

CryEngine 1 grouped the same services inside `CrySystem`: console and
console variables (`XConsole`, `XConsoleVariable`), `Log`, the frame profiler,
memory accounting, timers, and the pak file system
([cryengine1_subsystem_research.md](../cryengine1_subsystem_research.md)).
Its top level otherwise named one major subsystem per module - 3D engine,
renderer, physics, animation, entity system, AI, script system, sound, input,
font, network, game - which maps directly onto Cypher's existing top-level
folders.

## Decision

The runtime services are grouped under `CypherSystem`, and package archives
live under the file system:

```text
src/CypherSystem/
    Platform/   Cypher::System             OS, windows, timing, events, GL context (Sys_, GLimp_)
    Memory/     Cypher::Memory             arenas, pools, scratch, memory diagnostics (Mem_)
    Log/        Cypher::Log                records, formatting, sinks (Log_)
    Console/    Cypher::Console            commands, console variables, .cfg (Cmd_, Cbuf_, Cvar_, Cfg_)
    Profiler/   Cypher::Profiler           planned: capture, aggregation, reports, export
src/CypherFileSystem/
    (root)      Cypher::FileSystemRuntime  mounts, virtual paths, watches (FS_)
    Pak/        Cypher::Pak                package archives (Pak_)
```

- Each subfolder stays its own static library, so CMake still enforces the
  boundaries ADR 0006 asked for; only the folders move.
- Commands, console variables, and configuration become one `Cypher::Console`
  library, replacing `Cypher::CommandLegacy`, `Cypher::CVarLegacy`, and
  `Cypher::ConfigLegacy`: they are one service (a console executes commands,
  sets variables, and runs `.cfg` scripts), as XConsole was.
- Tests and benchmarks mirror the source folders
  (`tests/CypherSystem/Console`, `tests/CypherFileSystem/Pak`, ...).
- Headers keep basename includes; files that included System headers by path
  now use `CypherSystem/Platform/...`.
- `Profiler/` is created when its first code lands
  ([future_implementations.md](../future_implementations.md), section 5).

The top level keeps one folder per major subsystem, following the CryEngine
module list:

| CryEngine 1 | Cypher |
| --- | --- |
| CryCommon | `CypherCommon` |
| CrySystem | `CypherSystem` |
| CryPak (in CrySystem) | `CypherFileSystem/Pak` |
| Cry3DEngine | `CypherWorld` (name fixed by ADR 0004/0006) |
| RenderDll | `CypherRender` |
| CryPhysics | `CypherPhysics` |
| CryAnimation | `CypherAnimation` |
| CryEntitySystem | `CypherEntity` |
| CryAISystem | `CypherAI` |
| CryScriptSystem | `CypherScript` |
| CrySoundSystem | `CypherAudio` (spatial 3D audio inside, e.g. `Spatial/`) |
| CryInput | `CypherInput` |
| CryFont | `CypherFont` |
| CryNetwork | `CypherNetwork` (`NET_`, `Netchan_` prefixes) |
| CryGame | `CypherGame`, with `CypherClient` / `CypherServer` |
| FarCry launcher | `CypherEngine` (Host) |
| Editor | `CypherEditor` and Mason |
| ResourceCompiler | `CypherTools/Cypher*Compiler` |

Large subsystems grow internal subfolders rather than new top-level folders,
for example `CypherAudio/{Mixer, Spatial, Streaming, Backends}` and
`CypherRender/{Frontend, OpenGL, Vulkan}`. Empty placeholder folders are kept.

## Consequences

- The top level lists the engine's subsystems; the small services read as
  parts of the system layer.
- Consumers of the three legacy console targets link `Cypher::Console`.
- Documentation paths were updated; historical records keep their meaning
  with the new paths.

## Rejected alternatives

### One merged CrySystem library

Compiling all services into a single `CypherSystem` target would recreate the
catch-all module ADR 0006 rejected, and nothing would enforce, for example,
that the platform layer does not depend on logging.

### Keeping one top-level folder per service

Rejected by the project owner: the folders were too small to justify their
place at the top level.
