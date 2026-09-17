<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/six_month_engine_plan.md
//  Purpose: Audits the current engine state and defines the next six months.
//  Details: This plan ties work to verified repository evidence, vertical slices,
//           dependency gates, and explicit deferrals. It is the short-horizon
//           execution plan; long-term product documents remain directional.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine State Audit And Six-Month Execution Plan

Snapshot date: 2026-09-16

Planning horizon: 2026-09-16 through 2027-03-15

Primary objective: turn the existing foundations into one integrated, inspectable
runtime vertical slice rather than expanding the number of disconnected systems.

## How To Read This Plan

This document is the authoritative short-horizon execution plan. It does not
replace the long-term architecture in [master_plan.md](master_plan.md), the
phase definitions in [development_phases.md](development_phases.md), or the
module ownership decisions in the ADRs. It corrects their schedule assumptions
against the repository that exists now.

Status terms have narrow meanings:

| Status | Meaning |
| --- | --- |
| **Implemented** | Executable code exists, is wired into a CMake target, and has focused test or smoke coverage. |
| **Partial** | Useful executable code exists, but the required vertical slice or ownership boundary is incomplete. |
| **In progress** | The current milestone has begun, but its public/backend implementation and acceptance tests are not complete. |
| **Contract only** | Headers, documentation, or schemas reserve a boundary; no usable runtime implementation is claimed. |
| **Scaffold only** | The folder contains only placeholders such as `.gitkeep`. |
| **Deferred** | Deliberately outside this six-month critical path. |

An implemented component is not automatically a finished subsystem. A passing
unit test proves a bounded behavior; it does not prove that Host, resources,
renderer, World, and gameplay work together.

## Evidence Boundary

The audit is based on:

- the explicit build targets and source lists in [CMakeLists.txt](../CMakeLists.txt)
- committed source under `src/`, tools under `src/CypherTools/`, and their tests
- the current renderer contracts and OpenGL implementation under
  [`src/CypherRender`](../src/CypherRender)
- the cooked resource contracts under
  [`src/CypherCommon/Formats`](../src/CypherCommon/Formats)
- the VFS-backed render-asset loaders under
  [`src/CypherResource`](../src/CypherResource)
- the existing architecture, format, compiler, renderer, and World documents
- repository history from approximately 2026-08-05 through 2026-09-15

The local shader and backend drafts mark runtime shaders as the active milestone.
At audit start, runtime shader dispatch, OpenGL program creation, and shader
runtime acceptance tests were not implemented in the build. The author is editing
that track concurrently; subsequent shader files and declarations are work in
progress and are not verified by this non-renderer pass. Renderer status below
records that audit-start boundary until its owner closes the acceptance gate.

## Executive Assessment

CypherEngine is an unusually broad foundation project, not yet a usable game
engine runtime. Its strongest areas are common utilities, deterministic render
asset cooking, cooked format validation, synchronous resource ownership, VFS
contracts, and focused subsystem testing. The new renderer has a credible
frontend/backend boundary and working low-level OpenGL objects.

The missing value is integration.

The real executable currently creates a System window, but Host does not call
`R_ConfigureWindow`, `R_Init`, `R_BeginFrame`, `R_EndFrame`, or `R_Shutdown`.
`Host_Render` explicitly does no renderer work. The renderer can clear and
present in an isolated hidden-window smoke test, create buffers, and create
VAO-backed vertex-input objects, but it cannot create a program, bind a pipeline,
or issue a public draw. World has ownership documents and reserved headers but
no operations. Input, Entity, Physics, Audio, Client, Server, Game, Network,
Animation, AI, Script, Profile, Console, and the top-level Editor folder remain
scaffolds.

This is not a failure of architecture. It is the point at which architecture
must stop expanding and begin paying for itself through one complete path:

```text
.cyshader source
    -> CypherResourceCompiler
    -> validated CYSH bytes
    -> VFS and CypherResource ownership
    -> runtime renderer program
    -> immutable pipeline
    -> indexed draw
    -> first cube in the real Host loop
    -> reflected bindings and cooked material
    -> renderer-neutral World submission
    -> controllable static graybox
```

The expected six-month result is a small, reliable, game-facing runtime slice:
a real executable that loads cooked assets, renders a textured static graybox,
accepts player input, owns objects through a minimal World, and supports basic
collision-driven movement with useful diagnostics. A production editor,
networked game, terrain/streaming stack, advanced renderer, and complete combat
loop are not credible base commitments for this horizon.

## Current Buildable Islands

### Common And Data Foundations — Implemented, Broad, Not Uniformly Mature

`CypherCommon` is split into explicit Tier0, Tier1, and Tier2 targets, with
additional Math, Security, Image, RenderFormats, ResourceSystem, VFS, render
preview, and ToolFramework libraries. The repository contains extensive focused
tests and benchmarks for these families.

The useful conclusion is not that every Common file are complete. It is that the
specific contracts needed by the next vertical slice already exist:

- fixed-width types, errors, handles, allocators, blobs, spans, strings, hashes,
  byte readers/writers, schemas, and CYKV
- vectors, matrices, transforms, bounds, rays, frusta, and viewport math
- the `CYRS` cooked envelope and `CYSH`, `CYTX`, and `CYMT` render resources
- a provider-neutral VFS plus a loose-directory provider
- stable resource IDs and generation-checked runtime resource handles
- image decode/process/resize/mip contracts for the texture pipeline
- compiler and command-line host contracts used by ResourceCompiler

The next six months should consume these APIs. It should not add another broad
Common family unless an active vertical slice proves that it is missing.

### Offline Render Asset Path — Implemented Through Validated Runtime Views

The repository has a real source-to-runtime-data pipeline:

1. CYKV shader, texture, and material recipes have schemas and typed decoders.
2. `CypherShaderCompiler`, `CypherTextureCompiler`, and
   `CypherMaterialCompiler` create deterministic cooked outputs.
3. `CypherResourceCompiler` provides compiler discovery, validation, input
   discovery, reports, cancellation, and transactional publication.
4. `CypherResource` registers VFS-backed loaders for `.cyshader_c`, `.cytex_c`,
   and `.cymat_c`.
5. Typed accessors return validated zero-copy cooked views while the resource
   manager owns their backing blobs.

This slice ends before GPU object creation. The renderer must consume a
`cooked_shader_view_t`; it must not reopen raw GLSL, parse CYKV, or own VFS
policy. That boundary is the current milestone.

### System And Renderer Bootstrap — Partial

`CypherSystem` owns process/platform services, SDL window/event behavior, and
the native OpenGL context boundary. `CypherRender` owns backend selection,
configuration, lifecycle validation, capability reporting, frames, resize,
presentation, buffers, vertex formats/layouts, and vertex-input objects.

Current low-level evidence includes:

- renderer lifecycle and invalid-order tests
- hidden-window OpenGL clear/present smoke coverage
- generation-checked public buffer handles and private OpenGL buffer names
- buffer create, update, map, unmap, query, destruction, and retained ownership
- vertex-format and layout validation
- generation-checked vertex-input handles backed by OpenGL VAOs
- retained buffer references while a vertex-input object is alive

The Host integration gate is still open. The standalone renderer foundation
works in tests, while the shipped executable does not initialize it.

### Runtime Shell — Partial

Host, Log, Memory, FileSystem, Pak, Command, CVar, and Config contain executable
implementations and are used by the main process. Their short names were
standardized to `Host_`, `Log_`, `Mem_`, `FS_`, `Pak_`, `Cmd_`, `Cvar_`, and
`Cfg_`. Logger state and sink reconfiguration have focused concurrency coverage.

Two overlaps require restraint:

- `src/CypherFileSystem` is the older engine filesystem, while
  `CypherCommon/FileSystem` supplies the provider-neutral VFS used by the newer
  resource/compiler path.
- `src/CypherMemory` coexists with allocator and memory utilities in Common.

Do not grow both sides of either overlap. Use the newer provider-neutral VFS for
cooked resources. Delay consolidation until the renderer slice is visible, then
migrate one concrete call path at a time with tests.

### World And Game-Facing Runtime — Contract Or Scaffold Only

The World/renderer ownership ADR and module maps are detailed and internally
consistent. `src/CypherWorld` contains reserved error, local, public, and type
headers plus responsibility documents for Scene, Spatial, Visibility, Terrain,
Environment, Streaming, Submission, and Debug. It exports no runtime operation.

Every other game-facing folder is either a `.gitkeep` scaffold or similarly
non-executable. No current documentation may describe a playable scene,
movement controller, physics world, entity runtime, audio runtime, client/server
session, or gameplay loop as implemented.

### Tools And Editor — Useful Work Exists, But It Is Off The Critical Path

The ResourceCompiler and its three render-asset compiler modules are relevant
and active. Picasso has a substantial texture/material authoring core, Qt GUI,
licensed icon assets, and focused tests. Most other named tool-product folders
are placeholders.

Picasso should remain buildable, but new editor features are deferred until the
runtime can provide a real render-preview provider. The top-level
`src/CypherEditor` folder remains only a scaffold. Mason remains a documented
future product, not an implemented editor.

## Vertical Slice Audit

### Slice A — Deterministic Cooked Shader

**Status:** Implemented through `cooked_shader_view_t`.

**Already present:** CYKV recipe validation, include/define processing, glslang
preprocess/parse/cross-stage link validation, deterministic `CYSH` publication,
VFS loading, resource ownership, typed view access, format tests, compiler tests,
resource-loader tests, and benchmarks.

**Missing:** a live renderer program created from the view.

**Next concrete step:** implement the minimal synchronous runtime shader/program
object described in the renderer plan below.

**Unlocks:** immutable graphics pipelines and the first draw.

### Slice B — Renderer In The Real Executable

**Status:** Partial.

**Already present:** System window/context services, renderer lifecycle, clear
and present, Host renderer configuration storage, and CVar-derived present mode.

**Missing:** Host calls to preconfigure the window, initialize the renderer,
frame it, handle drawable resize/minimize, wait idle, and shut down before the
window is destroyed.

**Next concrete step:** close this gate while adding the first cube; reuse the
same `R_BeginFrame`/`R_EndFrame` path proven by the renderer smoke test.

**Unlocks:** a user-visible acceptance test instead of isolated component tests.

### Slice C — Colored Indexed Cube

**Status:** In progress at the shader milestone.

**Already present:** buffers, vertex layouts, vertex inputs, index types,
OpenGL context/bootstrap, and basic color shader source files.

**Missing:** runtime shader handles, OpenGL program objects, immutable pipeline
state, public draw validation/dispatch, and Host integration.

**Required order:** runtime shader from cooked `CYSH` -> minimal pipeline ->
draw -> first cube. Do not insert textures, material systems, render graphs,
World traversal, or a general command-buffer architecture before this gate.

**Exit:** the real `CypherEngine` process displays one depth-tested indexed cube
without raw OpenGL calls outside `src/CypherRender/OpenGL` and without loading
raw shader source at runtime.

### Slice D — Textured Cooked Material

**Status:** Planned; offline formats and loaders are implemented.

**Already present:** `CYTX` and `CYMT` formats, texture/material compilers,
resource loaders, typed cooked views, image processing, and material source
binding names.

**Missing:** shader reflection, frozen binding metadata, texture/sampler objects,
GPU upload, binding validation, material-instance ownership, and a draw path that
applies them.

**Required order:** first cube -> reflection design -> additive cooked shader
metadata/version decision -> texture and sampler -> material bindings -> textured
cube.

**Exit:** a `.cymat` recipe and referenced `.cytex` resources cook, load through
VFS/Resource, create renderer-owned objects, and shade the cube.

### Slice E — Minimal World Submission

**Status:** Contract only.

**Already present:** ownership ADR, module map, math primitives, resource handles,
and the rule that World selects coarse visible candidates while Renderer sorts
and draws them.

**Missing:** object table, transforms/bounds, query input, linear visibility
reference path, immutable submission, and renderer consumption.

**Required order:** textured material -> reusable mesh object/format pressure ->
small in-memory World -> renderer-neutral submission -> fully resident cooked
static map.

**Exit:** Host asks World for one view submission and Renderer consumes it without
Renderer traversing World state or World emitting backend commands.

### Slice F — Controllable Graybox

**Status:** Planned.

**Dependencies:** working Host renderer, Input, camera/view constants, minimal
World, static collision representation, collision queries, and a kinematic
controller.

**Required order:** input state/actions -> free camera -> static World -> ray/
sweep queries -> slide/step movement -> player camera.

**Exit:** a player can enter, move around, collide with, and inspect a cooked
static graybox while frame, resource, World, and collision diagnostics remain
available.

### Slice G — Minimal Game Loop

**Status:** Stretch goal, not a six-month commitment.

One target, one weapon interaction, damage/death, a reset command, and one audio
event are enough. Networking, prediction, AI navigation, animation graphs,
scripting, inventory, a general ECS, and a polished HUD remain deferred.

## Dependency Order

The critical path is deliberately narrow:

```text
System window/context
    -> renderer lifecycle
    -> buffers and vertex input                [implemented in isolation]

CYKV shader
    -> ResourceCompiler
    -> CYSH
    -> VFS
    -> CypherResource cooked view              [implemented]
    -> renderer shader program                 [current]
    -> pipeline
    -> draw
    -> first cube in Host
    -> reflection and binding metadata
    -> texture/sampler/material
    -> textured cube
    -> mesh/runtime object
    -> World submission
    -> input/camera
    -> cooked static graybox
    -> collision/controller
    -> optional gameplay slice
```

Anything not feeding this chain is maintenance-only during this horizon.

## Operating Model: Renderer Owner And Parallel Support

The project author owns renderer implementation work. The parallel support track
must increase confidence around the engine without competing for renderer design
or forcing its schedule.

| Track | Owner | Current scope | Handoff condition |
| --- | --- | --- | --- |
| Renderer | Project author | R1 cooked `CYSH` programs, then pipeline, draw, first cube, reflection, and material bindings | The author explicitly declares a contract stable or requests integration help. |
| Parallel support | Collaborator/agent | Tests for existing non-renderer behavior, diagnostics, documentation/build hygiene, and small dependency-safe subsystem improvements | A change needs a renderer API, cooked-format version, Host renderer order, or shared CMake ownership decision. |
| Shared integration | Explicitly assigned per task | `CMakeLists.txt`, Host/Renderer startup, Common render formats, Resource-to-Renderer adapters, and cross-system acceptance tests | Both tracks agree on the stable interface and one owner makes the shared edit. |

The shared status record is this document plus
[current_status.md](current_status.md). Each active item should record its owner,
status, files or boundary in scope, dependency, verification evidence, and next
handoff. Change **Implemented** only after target wiring and tests exist; do not
use percentage movement as a substitute for that evidence.

Parallel work is non-blocking only when it:

- builds and tests independently of unfinished renderer code
- does not edit `src/CypherRender` or pre-empt its public descriptors
- does not change `CYSH`, `CYTX`, or `CYMT` versions or semantics without a
  renderer-driven requirement
- does not add a dependency edge into Renderer, Host, or Resource integration
- is small enough to rebase, review, or discard without delaying the renderer

Priority order for the parallel track:

1. add missing failure, lifetime, concurrency, and platform tests around existing
   Resource, VFS/FileSystem, Pak, Memory, Log, Command, CVar, and Config behavior
2. make small diagnostics or correctness improvements exposed by those tests
3. keep CMake, CI, changelog, and status documentation accurate
4. prepare isolated test fixtures that the renderer can consume after its public
   contract stabilizes

World, Physics, Audio, networking, new cooked formats, and editor features are
not parallel filler work. Starting them early would create dependencies and
design pressure that can block or diverge from the renderer critical path.

### Non-Renderer Work Queue

This queue makes the parallel mandate actionable. Completion requires recorded
verification, not merely an existing folder or related Common utility. Independent
slices may run in parallel with explicit file ownership. See the
[runtime work log](non_renderer_work_log.md) for the current checks and limits.

| ID | Owner / status | Scope and acceptance | Dependencies / next handoff |
| --- | --- | --- | --- |
| NR-01 | Agent / complete, 2026-09-16 | Runtime `Mem_Pool*` public-API tests: borrowed storage, aligned unique slots, exhaustion/reuse, invalid frees, bitmap boundaries, counters, reset, zero policies, and mixed-operation payload integrity. Add a separate CTest target; run Debug and ASan/UBSan. Measure Release sequential versus shuffled reuse at selected capacities. | Existing Pool/Arena/System/Log only. No renderer source or format changes. Eight cases / 58,552 assertions pass in Debug and ASan/UBSan; four Release workloads run for five repetitions. [Evidence and limits](non_renderer_validation_2026-09-16.md). Next: NR-02. |
| NR-02 | Agent / bounded pass complete, 2026-09-16 | Runtime arena, scratch, bucket, wrapper and global memory lifetimes. Fix clear-before-decommit, stale diagnostic state and wrapper synchronization; cover rollback, alignment, checked arithmetic and ownership. | Five runtime memory suites pass Debug and ASan/UBSan; wrappers pass TSan. Release pool and arena workloads pass. OS failure injection remains explicitly limited. |
| NR-03 | Agent / bounded pass complete, 2026-09-16 | Resource failure/dependency teardown; malformed Pak data and transactional publication; FileSystem cancellation, reopen ownership and shutdown admission. | Debug and ASan/UBSan Resource/VFS/Pak/FS checks pass; FS also passes TSan. Existing async API is repaired without a new model; no cooked format or renderer changes. Cross-lifecycle request identities remain a documented follow-up. |
| NR-04 | Agent / bounded pass complete, 2026-09-16 | Command/CVar/Config argument/value limits, recursion, numeric conversion and aliasing; Log sink validation and failure preservation; System baseline. | Command/config/log pass Debug and ASan/UBSan; Log also passes TSan. The 230-test graphics-independent Debug selection passes, and System benchmarks run. Host remains with its integration owner. |
| NR-05 | Agent / design candidate | Input frame-state reducer using existing System key/mouse/focus events: held/pressed/released state, repeat handling, accumulated deltas, focus-loss and queue-overflow recovery. Prove behavior with synthetic events before Host routing. | System events already exist; pure state tests need no renderer. Discuss the small Input contract before implementation; Host routing is a separate integration handoff. |

Entity, World, Physics, Audio, Client/Server, Network, Animation, AI, Script,
Profile, Console, and Editor remain accounted for in the folder audit and
dependency gates below. They are future scope, not omissions to fill with empty
implementations. This queue may be reordered when the author identifies a
concrete gameplay need or a failing invariant.

## Renderer Execution Plan

This section supersedes dates in older renderer plans while preserving the
ownership rules in [renderer_host_module_map.md](renderer_host_module_map.md).

### R0 — Existing Foundation

**Status:** Implemented in the renderer target; Host integration remains open.

Keep these contracts stable while completing the first draw:

- frontend lifecycle and configuration validation
- private backend dispatch table
- OpenGL capability/context implementation
- generation/type-checked handles
- buffer ownership and retained references
- vertex-format, layout, and vertex-input validation
- OpenGL VAO implementation

Do not redesign these modules unless the shader or first draw exposes a concrete
defect.

### R1 — Runtime Shader Programs From Cooked CYSH

**Status:** Current milestone, in progress.

The first public shader contract should be intentionally small:

- one graphics program with exactly one vertex and one fragment stage
- input is an already validated `cooked_shader_view_t`
- synchronous creation; the renderer borrows cooked bytes only for the call
- a generation/type-checked public shader handle
- backend-private shader/program tokens and native OpenGL names
- create, destroy, query, and stable diagnostic/error behavior
- optional debug name with explicit copy/ownership rules
- no raw path, VFS, CYKV, glslang, or resource-manager dependency in Renderer

The frontend must still check backend/profile compatibility and the required
stage set. The OpenGL backend compiles each prepared GLSL stage, captures bounded
driver diagnostics, links the program, detaches/deletes temporary stage objects,
and publishes the program only after complete success. Every failure path must
delete partial native objects and leave the output handle invalid.

`CypherRender` will need the narrow cooked shader declaration available to its
public contract. Prefer linking the existing RenderFormats target over copying
format types or moving resource ownership into Renderer.

Acceptance tests:

1. public contract signature/static checks
2. rejection before renderer initialization
3. rejection of incompatible backend/profile/stage data
4. failed GLSL compile and failed link leave no live handle
5. hidden OpenGL context creates, queries, and destroys one program
6. stale and wrong-type handles are rejected
7. VFS -> Resource -> `Res_GetCookedShader` -> `R_CreateShader` integration test

Reflection is explicitly excluded from R1. The current `CYSH` version 2 contract documents reflection
as deferred, so it must not be silently inferred as a stable material ABI here.

### R2 — Minimal Immutable Graphics Pipeline

**Status:** Planned immediately after R1.

Design only the state required by the cube:

- shader handle
- vertex-layout compatibility key or descriptor
- triangle-list topology
- front-face and cull mode
- depth test, compare, and write enable
- one color target with blending disabled
- sample-count/default-target compatibility
- generation/type-checked pipeline handle

OpenGL may store validated state rather than creating a native monolithic
pipeline object. The public object is still immutable. It retains its shader
reference so destroying the caller's shader handle cannot invalidate a live
pipeline.

Acceptance tests cover invalid enum values, missing shader/layout, incompatible
layout, retained ownership, stale handles, and backend state application. Do not
add stencil, multiple render targets, dynamic blend modes, specialization,
permutations, or render-pass graphs until a later visible slice needs them.

### R3 — Draw And First Cube

**Status:** Planned immediately after R2.

The first draw contract needs:

- active-frame validation
- pipeline and vertex-input handles
- indexed range, index count, first index, base vertex, and instance count
- overflow-safe validation against index-buffer bounds
- layout/pipeline compatibility checks
- backend dispatch to one indexed draw

A large retained command-buffer system is not a prerequisite. A direct validated
frontend dispatch or one bounded internal command record is sufficient for the
first cube. General recording is introduced only when World submission, sorting,
or multithreading creates real pressure.

Host closure belongs in this gate:

1. call `R_ConfigureWindow` before `Sys_CreateWindow`
2. initialize Renderer after window creation
3. roll back Renderer before destroying the System window on failure
4. begin/end frames in Host
5. skip or safely resize zero-sized/minimized drawables
6. shut down Renderer before the window

The cube is built from renderer buffers and vertex input, uses a cooked `CYSH`
program, and draws through the public pipeline/draw APIs. Its vertices may use a
fixed clip-space transform for this gate so reflection/material work remains in
the next milestone.

Acceptance is visual and automated where practical: the real process reaches a
stable frame loop; the hidden-context smoke test draws without GL errors; a
debug readback or deterministic screenshot check can be added after the draw is
correct, not before.

### R4 — Reflection And Binding Contract

**Status:** Planned only after the first cube.

This milestone needs an explicit format decision. `CYSH` version 2 contains
program/stage metadata and code but no reflected resource layout. Material
compatibility should be deterministic at cook time, not discovered differently
on each GPU driver.

Preferred direction:

1. define backend-neutral reflected records for vertex inputs, uniform/storage
   blocks, textures, samplers, stage visibility, array count, binding number,
   and stable name/hash
2. extract and validate reflection in `CypherShaderCompiler`
3. add reflection as an additive `CYSH` chunk under a deliberate format-version
   rule
4. make the runtime compare driver-visible bindings against cooked metadata in
   validation builds
5. use cooked reflection as the material compatibility contract

Do not expose raw OpenGL locations as persistent asset data. Do not freeze a
general Vulkan descriptor model before a second backend exists.

### R5 — Texture, Sampler, And Material Bindings

**Status:** Planned after R4.

Sequence:

1. texture object from a validated `cooked_texture_view_t`
2. immutable sampler object with only required filter/address policy
3. binding-set description keyed by cooked shader reflection
4. material runtime that retains pipeline, texture, sampler, and parameter data
5. renderer/resource adapter that translates retained cooked resources into
   renderer objects without merging manager and renderer ownership
6. textured, depth-tested cube through the same public draw path

Material loading failure must be transactional. A missing texture, incompatible
binding name/type, or GPU upload failure leaves the previous material live and
does not leak renderer handles.

### R6 — Mesh And World Submission Pressure

**Status:** Planned late in the horizon.

Only after the textured cube is stable should the renderer define a reusable
mesh object and the minimum cooked mesh contract. The first mesh needs vertex
streams, index data, bounds, submesh ranges, and material references—nothing
more. World supplies visible candidates and transforms; Renderer classifies,
sorts, and draws them.

Render queues, command storage, view constants, debug lines, and offscreen
targets enter here only as required by the first World submission. Shadows,
advanced lighting, post-processing, GPU occlusion, clustered rendering, virtual
texturing, and a render graph remain deferred.

## Folder-By-Folder Audit And Next Step

### Active Runtime And Foundation Folders

| Folder | Status and evidence | Dependency / next concrete step |
| --- | --- | --- |
| `src/CypherCommon` | **Implemented in many bounded libraries.** Tier0/1/2, Math, Security, Image, formats, VFS contracts, ResourceSystem, ToolFramework, tests, and benchmarks exist. Breadth is not a claim that every utility is production-complete. | Freeze broad expansion. Fix only defects or missing primitives encountered by the active renderer/World slice. |
| `src/CypherSystem` | **Implemented/partial.** Explicit target owns platform services, SDL windows/events, OpenGL context services, and target-specific translation units. Focused runtime/window/event/OpenGL tests exist. | Support Host renderer lifecycle, drawable resize/minimize, and clean rollback. Avoid moving renderer policy into System. |
| `src/CypherPlatform` | **Scaffold only.** Native runtime ownership was consolidated under System and Common target queries. | Keep empty during this horizon. Do not create a competing platform abstraction without an ADR-backed need. |
| `src/CypherLog` | **Implemented for the current phase.** Runtime filtering, sinks, formatting, synchronized configuration, transactional sink replacement, and concurrency tests exist. | Maintenance only. Preserve emergency/bootstrap output separation when System fatal paths are completed. |
| `src/CypherMemory` | **Implemented/partial.** Arena, pool, bucket, scratch, and thread-aware code plus benchmarks exist; runtime pool tests pass in Debug and ASan/UBSan. Common also has separate allocator primitives. | Use existing allocators in active systems. Record real lifetime pressure before consolidating or expanding either memory family. |
| `src/CypherFileSystem` | **Implemented legacy runtime path.** Mount/read/write/package/discovery/watch/async modules and smoke coverage exist. A newer provider-neutral Common VFS also exists. | Do not extend both APIs. Use Common VFS for cooked assets; schedule incremental legacy migration only after first cube. |
| `src/CypherPak` | **Implemented for current needs.** Reader/writer/compression, package-backed legacy filesystem mounts, tests, and benchmark exist. | Maintenance only until a real build/package workflow requires format or streaming changes. |
| `src/CypherResource` | **Implemented synchronous core; partial renderer integration.** Type registry, handles, cache/refcounts, rollback, cycle checks, shutdown, render-asset loaders, tests, and benchmarks exist. | Supply cooked shader views to Renderer, then add renderer-facing residency/adapters without storing native GPU objects in Common. |
| `src/CypherRender` | **Partial and active.** Lifecycle, backend, OpenGL, buffers, vertex layouts, and vertex inputs are implemented. Shader is in progress; pipeline, draw, and command are contract placeholders. | Execute R1-R6 in order. First hard gate is a runtime program from cooked `CYSH`; next is pipeline/draw/first cube. |
| `src/CypherEngine` | **Partial.** Host initializes core runtime systems and a System window, owns the frame loop, and applies startup CVars. It currently omits renderer lifecycle/frame calls. | Integrate renderer in the first-cube gate with strict startup rollback and shutdown order. Keep `main.cpp` thin. |
| `src/CypherCommand` | **Implemented basic runtime.** Registry, duplicate checks, fixed argument parsing, and callback execution exist. | Maintenance only; use commands for renderer/resource/World diagnostics before adding a separate console UI. |
| `src/CypherCVar` | **Implemented basic runtime.** Registry, flags, cached typed values, and set/find/get paths exist. | Add renderer settings only when their runtime mutation behavior is real and tested. |
| `src/CypherConfig` | **Implemented basic runtime.** Config loading, line execution, `exec`, `set`, `seta`, and command fallback exist. | Keep startup policy working while renderer CVars gain real effects; avoid a format rewrite. |
| `src/CypherWorld` | **Contract only.** Ownership ADR, reserved public/private headers, module map, and folder responsibility documents exist; no runtime operation or target exists. | After textured material/mesh pressure, implement Gate 1 only: object table, transforms/bounds, linear view query, immutable submission. |
| `src/CypherTools` | **Mixed.** ToolFramework consumers, ResourceCompiler, shader/texture/material compilers, Picasso core/GUI, and tests exist; most named product folders are placeholders. | Maintain compilers; extend shader cooking for reflection after first cube. Freeze new tool products and Picasso features until runtime preview exists. |

### Game-Facing And Later Runtime Folders

| Folder | Current status | Six-month role / next step |
| --- | --- | --- |
| `src/CypherInput` | **Scaffold only.** | Month 4: keyboard/mouse snapshot plus action mapping sufficient for a free camera. No rebinding UI or device abstraction beyond actual needs. |
| `src/CypherEntity` | **Scaffold only.** | Defer a general entity/component framework. World owns static object IDs/transforms first; introduce Entity only for game-owned dynamic identity pressure. |
| `src/CypherPhysics` | **Scaffold only.** | Month 6: static query interface, raycast, capsule/box sweep, slide/step controller, and debug draw. Decide custom versus third-party rigid bodies later. |
| `src/CypherAudio` | **Scaffold only.** | Deferred base scope. One event/source can be a stretch goal after controllable graybox; no mixer graph or spatial-audio architecture yet. |
| `src/CypherClient` | **Scaffold only.** | Deferred. Do not invent a client module before local Host/Game/World ownership is exercised. |
| `src/CypherServer` | **Scaffold only.** | Deferred. Preserve the coop-first listen-server ADR, but no networking work enters the critical path. |
| `src/CypherGame` | **Scaffold only.** | Stretch: thin REAP-specific rules for one target/weapon/reset after movement works. Keep engine ownership outside Game. |
| `src/CypherNetwork` | **Scaffold only.** | Deferred beyond six months. No sockets, replication, prediction, or protocol work before a deterministic local loop. |
| `src/CypherAnimation` | **Scaffold only.** | Deferred. Static meshes and camera movement are enough for the target slice. |
| `src/CypherAI` | **Scaffold only.** | Deferred. A stationary target can exercise damage without navigation or behavior systems. |
| `src/CypherScript` | **Scaffold only.** | Deferred. Do not start RVM or a native/script bridge before game-facing APIs stabilize. |
| `src/CypherProfile` | **Scaffold only.** | Use existing Common timing/profile primitives first. Promote a dedicated subsystem only when frame/resource/World timings need aggregation and UI. |
| `src/CypherConsole` | **Scaffold only.** | Deferred UI. Command/CVar/Config and log output already provide the needed diagnostic backbone. |
| `src/CypherEditor` | **Scaffold only.** | Deferred. Picasso lives under Tools; Mason and a unified editor wait for a stable runtime preview and World data contract. |

## Size And Completion Estimates

These figures are planning aids, not productivity targets or promises. Lines of
code measure repository volume, not correctness, integration, difficulty, or
value. A small World handoff can be more important than thousands of utility
lines, and deleting an obsolete path can be progress.

### Current Measurable Size

The reproducible baseline is commit
`e5e0972ccde9687dd44bb4eac97f943f33f4e9df` (2026-09-15), before local shader
drafts and this documentation/test work. Enumerate its blobs with
`git ls-tree -r`, read them with `git cat-file`, and count newline bytes. These
are physical lines including comments and blanks, not language-aware source
lines. Code categories include `.c`, `.cc`, `.cpp`, `.cxx`, `.h`, `.hpp`, and
`.inl`; inline implementations are part of the source total.

Documentation is root-level Markdown plus Markdown under `docs/`. Build/dev
support is exactly root `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`,
`src/CypherTools/CMakeLists.txt`, `.github/workflows/ci.yml`, the five
`cmake/*.cmake` files, `tools/dev/generate_clangd_compile_db.py`, and
`tools/perf/{CypherPerf_SystemReport.cpp,compare_benchmarks.py,run_benchmarks.py}`.
The selected categories exclude vendor/external code, generated builds, binary
assets, source-folder READMEs, and all uncommitted changes; this is a subtotal
of defined first-party categories, not a count of every repository text file.

| Area | Tracked physical lines | What is included |
| --- | ---: | --- |
| Runtime/tool source | 163,703 | C/C++ headers and implementation files under `src/` |
| Tests | 49,644 | C/C++ under `tests/` |
| Benchmarks | 15,065 | C/C++ under `benchmarks/` |
| Documentation | 13,631 | Root and `docs/` Markdown at the pinned commit |
| Build/toolchain support | 5,146 | CMake, presets, manifests, CI, and tracked build/dev scripts |
| **Selected first-party total** | **247,189** | Sum of the categories above; categories do not overlap |

The source distribution is highly uneven: `CypherCommon` accounts for 118,290
physical C/C++ lines, while `CypherRender` has 4,781, `CypherWorld` has 180
contract-only lines, and many game-facing folders have zero C/C++ lines. This is
why the repository's total size must not be read as engine completion.

### Projected Size At The Six-Month Milestone

If the expected graybox path is reached without broad editor/networking work, a
reasonable range is:

| Area | 2027-03-15 planning range | Main sources of growth |
| --- | ---: | --- |
| Runtime/tool source | 180k-225k | shader/pipeline/draw, texture/material/mesh, Input, minimal World/map path, collision/controller |
| Tests | 65k-90k | renderer failure paths, cross-system resource tests, World/collision correctness, Host integration |
| Benchmarks | 16k-22k | selected renderer/resource/World query baselines, not blanket microbenchmarks |
| Documentation/build support | 22k-32k | format/ADR updates, map/compiler contract, CI and target wiring |
| **Projected first-party total** | **about 283k-369k** | Physical-line range, not a delivery quota |

The wide range reflects unknowns in shader reflection, the minimum mesh/map
format, platform-specific graphics coverage, and whether existing runtime paths
are migrated or retained. Finishing below the range through reuse or deletion is
not a miss. Exceeding it is not success unless the vertical slice is stronger.

### Broader Intended Engine Scope

For a mature Cypher runtime plus the necessary production asset pipeline,
gameplay framework, networking, diagnostics, Mason/Picasso-class tools, and
editor workflows, the existing long-term plan's order-of-magnitude remains
reasonable:

- approximately **400k-800k first-party production source lines**
- approximately **150k-350k test and benchmark lines**
- approximately **40k-90k documentation, schema, build, CI, and migration lines**
- approximately **590k-1.24M physical first-party lines in total**

That range has very high uncertainty and spans years, not six months. Platform
count, renderer backend count, custom versus third-party physics/audio, shipped
game requirements, editor ambition, and how many proposed tool products survive
real use can move it by at least tens of percent. It is a scope illustration,
not an instruction to create files.

### Approximate Completion And Work Remaining

The percentages below estimate functional breadth, integration, tests, and
diagnostics—not lines written. The six-month columns use the expected cooked
graybox milestone as the denominator. The broader columns use the intended
mature engine/tool ecosystem as the denominator. Ranges deliberately avoid
false precision; "remaining" is the inverse band.

| Area | Current evidence | Toward six-month milestone | Remaining to six-month milestone | Toward broader intended scope | Remaining to broader intended scope |
| --- | --- | ---: | ---: | ---: | ---: |
| Foundations and runtime shell | Common tiers, math, memory, logging, System, Host, command/CVar/config, filesystem and Pak are substantial; integration/consolidation remains. | 65-80% | 20-35% | 35-50% | 50-65% |
| Renderer | Lifecycle, OpenGL bootstrap, buffers, layouts, and vertex input exist; shader, pipeline, draw, material, scene submission, and advanced features do not. | 25-35% | 65-75% | 10-15% | 85-90% |
| Assets and resource pipeline | Deterministic shader/texture/material cooking, VFS loading, typed cooked views, and synchronous resource ownership exist; mesh/map formats, reflection, GPU residency, reload, and production dependency workflows remain. | 60-75% | 25-40% | 35-50% | 50-65% |
| Input and camera | Math and old design experience exist, but the current top-level Input folder is a scaffold and the rebuilt renderer has no view path. | 0-10% | 90-100% | 0-5% | 95-100% |
| World and scene | Ownership ADRs and module plans exist; runtime operations, object storage, queries, submissions, and cooked loading do not. | 5-10% | 90-95% | 0-5% | 95-100% |
| Physics and collision | Only common math/intersection foundations exist; the Physics folder is a scaffold. | 0-5% | 95-100% | 0-5% | 95-100% |
| Audio | Scaffold only and outside the base six-month commitment. | Not base scope | Not base scope | 0-5% | 95-100% |
| Gameplay and entity runtime | Game/Entity/Client/Server folders are scaffolds; no local gameplay loop exists. | 0-5% | 95-100% for stretch slice | 0-5% | 95-100% |
| Tools and editor | ResourceCompiler and render compilers are real; Picasso has a substantial initial core/GUI; most tool products, Mason, and top-level Editor are placeholders. | 45-60% of required tools | 40-55% | 10-20% | 80-90% |
| Build, dependencies, and CI | Explicit foundation targets, presets, pinned dependencies, multi-platform CI, sanitizers, and tool presets exist; more runtime targets and integrated graphics coverage remain. | 65-80% | 20-35% | 45-60% | 40-55% |
| Networking | Security primitives and a coop-first ADR exist, but the Network/Client/Server implementations do not. | Deferred | Deferred | 0-5% | 95-100% |
| Animation, AI, and scripting | Math/data contracts and long-term boundaries exist; runtime folders are scaffolds. | Deferred | Deferred | 0-5% | 95-100% |
| Tests and documentation | Large Common/tool coverage and detailed architecture docs exist; cross-system runtime tests and several current-status corrections remain. | 60-75% | 25-40% | 35-50% | 50-65% |

Uncertainty is highest for renderer reflection, World/map representation,
physics integration, networking, and editor products because their accepted
runtime requirements do not yet exist. Re-estimate these bands at each monthly
gate; do not mechanically increase a percentage because time passed or line
count grew.

## Six-Month Projection

Dates are gates, not promises. If a gate misses its acceptance criteria, the next
month continues that gate; later scope is removed rather than stacked on top of
an unstable base.

### Month 1 — 2026-09-16 To 2026-10-15

**Theme:** cooked shader to live renderer program.

Deliver:

- finish the runtime shader/program contract and ownership rules
- add backend callbacks and OpenGL compile/link/diagnostic behavior
- link the renderer to the narrow cooked shader contract
- add lifecycle, failure rollback, handle, OpenGL smoke, and
  VFS/Resource-to-Renderer integration tests
- design the minimal pipeline descriptor only after shader behavior is proven

Exit gate:

- a cooked `CYSH` loaded through VFS and CypherResource creates and destroys a
  live OpenGL program with no raw runtime shader file access

### Month 2 — 2026-10-16 To 2026-11-15

**Theme:** pipeline, draw, and first cube.

Deliver:

- immutable minimal pipeline with retained shader ownership
- validated indexed draw dispatch
- Host window preconfiguration and renderer init/frame/resize/shutdown integration
- one colored, depth-tested indexed cube in the real executable
- focused diagnostics for program, pipeline, vertex input, and draw failures

Exit gate:

- launching `CypherEngine` exercises the same public renderer contracts as tests
  and displays the cube without OpenGL calls outside the backend

### Month 3 — 2026-11-16 To 2026-12-15

**Theme:** reflection and cooked material binding.

Deliver:

- decide and document the additive/versioned `CYSH` reflection representation
- extract binding metadata in ShaderCompiler and validate it at cook time
- implement renderer texture and sampler objects from `CYTX`
- implement the smallest binding/material runtime compatible with `CYMT`
- render the cube through a cooked material and cooked texture

Exit gate:

- shader/material incompatibility fails during cooking or controlled resource
  creation, not as an unexplained wrong draw

### Month 4 — 2026-12-16 To 2027-01-15

**Theme:** reusable scene data, input, and camera.

Deliver:

- define a minimal renderer mesh object and only the cooked mesh fields proven
  necessary by the cube/material path
- implement keyboard/mouse snapshots and a small action map
- implement camera/view constants and a free camera
- implement World Gate 1: generation-safe static objects, transforms, bounds,
  linear visibility query, immutable submission
- render several in-memory objects through World -> Renderer handoff

Exit gate:

- the user can move a camera through multiple resource-owned objects, and
  Renderer never traverses mutable World state

### Month 5 — 2027-01-16 To 2027-02-15

**Theme:** fully resident cooked graybox.

Deliver:

- freeze the smallest source/cooked map subset needed for static mesh instances,
  transforms, bounds, material references, and spawn/view metadata
- implement deterministic map cooking or a deliberately narrow fixture compiler
- load one fully resident map through VFS/Resource into World
- add reference spatial queries, frustum/distance filtering, World/resource
  diagnostics, and debug bounds/lines
- profile before selecting any acceleration structure

Exit gate:

- no hardcoded object list is required to enter and render the test graybox

### Month 6 — 2027-02-16 To 2027-03-15

**Theme:** collision-driven runtime and stabilization.

Deliver:

- define Physics query ownership against static World collision data
- implement raycast and the minimum sweep used by a capsule or box controller
- implement slide, grounding, and a bounded step policy
- connect input, controller, and player camera
- add collision/resource/renderer debug commands and representative tests
- run Debug, Release, sanitizer, and platform CI passes; fix integration defects

Exit gate:

- a controllable player can move through and collide with the cooked graybox for
  a sustained test session without lifetime, stale-handle, or shutdown errors

### Expected Position On 2027-03-15

If the base plan succeeds, CypherEngine should have:

- one real Host-integrated OpenGL renderer path
- runtime programs sourced exclusively from cooked `CYSH`
- immutable pipelines, buffers, vertex inputs, textures, samplers, bindings,
  materials, meshes, and indexed draws at the level needed by the slice
- deterministic shader/texture/material and minimal mesh/map cooking
- VFS/Resource-owned runtime asset lifetimes
- keyboard/mouse input, free/player camera, and renderer view constants
- a minimal World with static objects, bounds, reference visibility, and immutable
  renderer submissions
- one fully resident cooked graybox
- static collision queries and a basic kinematic player controller
- tests and diagnostics around the cross-system path

It should not claim:

- production rendering, Vulkan, a render graph, advanced lighting, or shadows
- terrain, vegetation, portals, streaming, or large-world support
- general rigid-body physics
- a general ECS or mature gameplay framework
- multiplayer, prediction, or replication
- animation, AI, scripting, or a complete audio runtime
- Mason, a unified editor, or production content workflows
- a shippable REAP game

### Conservative And Stretch Outcomes

**Conservative outcome:** the first three months consume most of the horizon.
The engine finishes a robust textured material cube and Host integration, with
World/Input design ready but not implemented. This is still valuable because it
closes the most important broken end-to-end path.

**Expected outcome:** the base projection above: cooked textured graybox,
minimal World, input/camera, and collision-driven movement.

**Stretch outcome:** one stationary target, one weapon interaction, damage/reset,
one audio event, frame/resource diagnostics, and a captured reproducible demo.
Stretch work starts only after Month 6 acceptance passes.

## Assumptions

The projection assumes:

- one primary developer continues at a steady learning-focused pace
- one active milestone and one subsystem boundary are worked at a time
- OpenGL remains the only active renderer backend
- desktop platforms and the existing SDL/GLAD stack remain supported
- cooked formats may evolve deliberately before a public compatibility promise
- the existing Common, VFS, Resource, compiler, math, and image contracts are
  consumed rather than rewritten
- CI/dependency availability remains comparable to the current repository
- Picasso, Mason, networking, terrain, and broad new subsystem work stay frozen
- failures extend the current gate and remove later scope

## Principal Risks And Responses

| Risk | Why it matters | Response |
| --- | --- | --- |
| Component tests hide integration gaps | The renderer passes isolated tests while Host does not use it. | Every phase ends in a cross-system executable or resource integration test. |
| Two filesystem paths keep diverging | Assets may behave differently in tools and runtime. | Use provider-neutral VFS for all new cooked assets; migrate legacy calls incrementally after first cube. |
| Memory APIs overlap | New systems may choose ownership inconsistently. | Use existing system allocator/handle tables first; document lifetime at each public boundary before introducing another allocator. |
| Reflection freezes the wrong ABI | `CYSH` v2 explicitly defers reflection, while materials need deterministic compatibility. | Finish first cube without reflection, then make one explicit versioned format decision driven by actual bindings. |
| OpenGL-specific concepts leak upward | Fast progress can expose native names/locations in World or resources. | Keep native objects in `CypherRender/OpenGL`; public APIs use handles, descriptors, and cooked metadata. |
| World scope expands into terrain/streaming | The World documents describe many later systems. | Implement only Gate 1 and a fully resident static map. Add acceleration after profiling the reference path. |
| Editor work competes with runtime | Picasso already demonstrates how quickly tool scope can grow. | Maintenance only until runtime preview and World contracts are usable. |
| A general ECS or physics engine is started too early | Both can consume the horizon without producing a playable slice. | Static World objects and query-driven player movement first; general dynamic systems remain separate decisions. |
| Cross-platform graphics behavior differs | Context versions, debug output, and driver GLSL diagnostics vary. | Keep capability checks explicit, capture bounded diagnostics, and exercise hidden-context smoke tests in CI where supported. |
| Documentation gets ahead of code | Several older documents still describe retired renderer behavior. | Use the status vocabulary here; mark completion only with source, target wiring, and tests. Update this snapshot at every gate. |

## Immediate Work Queue

This is the concrete order from the snapshot date:

1. write the shader ownership/lifetime and error contract in pseudocode
2. finish `CypherRender_Shader.h` with the minimum public descriptor, handle,
   info, create, destroy, and query operations
3. extend the private backend table with shader program callbacks
4. implement frontend shader handle storage and transactional rollback
5. implement OpenGL stage compile, bounded diagnostics, link, cleanup, and program
   destruction
6. add renderer contract/runtime/OpenGL shader tests
7. add the VFS/Resource cooked shader to renderer integration test
8. design and implement the minimal immutable pipeline
9. add validated indexed draw dispatch
10. integrate renderer lifecycle and the first cube into Host

Do not start reflection or material bindings until item 10 is visibly complete.

## Gate Review Checklist

At the end of each milestone:

1. demonstrate the vertical behavior, not only a compile
2. run focused tests for changed contracts and failure paths
3. run the relevant Debug build and CTest subset
4. run sanitizers for ownership-heavy changes when supported
5. confirm no backend-native object escaped its owning module
6. confirm every new handle has stale/type validation and shutdown cleanup
7. update this document's status and next gate
8. update `current_status.md` if the active milestone changed
9. add factual changelog entries; do not copy future work into the changelog
10. cut later scope if the exit criterion is not met

## Maintenance Rule

Update the snapshot date only when the folder audit, active milestone, and next
queue have been rechecked against source and CMake. Move an item to
**Implemented** only when executable code, target wiring, and relevant test or
smoke coverage all exist. Keep abandoned or superseded design ideas in Git
history or an ADR; do not let this execution plan accumulate multiple competing
orders.
