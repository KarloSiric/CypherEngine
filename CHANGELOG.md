<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CHANGELOG.md
//  Purpose: Records notable CypherEngine project changes.
//  Details: This document is the chronological project memory for engine work. Keep
//           entries factual, dated, and separated from aspirational roadmap items.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Changelog

All notable changes to CypherEngine and the REAP game/runtime direction are tracked here.

## [Unreleased] - 2026-07-09

### Added
- Added a project-local vcpkg bootstrap that reads and checks out the exact registry revision pinned by `vcpkg.json`.
- Added feature-scoped dependency groups for tests, benchmarks, math, scripting, compression, security, text, images, textures, meshes, audio, archives, networking, profiling, shader tools, and editor support.
- Added pinned Dear ImGui, cgltf, and MikkTSpace Git submodules alongside the existing generated GLAD source.
- Added centralized CMake dependency targets and third-party acquisition, patching, external-SDK, provenance, and distribution policies.
- Added a CI manifest job that validates every approved vcpkg feature without forcing all optional tool dependencies into normal runtime builds.
- Added `docs/cyphercommon_architecture.md` to define CypherCommon as the shared public/common foundation and contract layer.
- Documented the intended CypherCommon folder families for Tier0/Tier1/Tier2/Tier3, utility code, memory, text, color, IO, hashing, parsing, serialization, reflection, formats, jobs, assets, resources, scene/world/entity data, renderer/audio/physics/network contracts, GUI, tools, and editor contracts.
- Added a function-pointer policy for C-style service tables, allocator interfaces, stream callbacks, backend dispatch, tool/plugin boundaries, command callbacks, and VM/native bridges.
- Added `docs/function_pointer_policy.md` to document direct calls, handles, command queues, event queues, callback tables, service tables, and subsystem communication rules.
- Added trackable `src/CypherCommon/` folder READMEs for the planned Common families, including Core, Sys, Utl, Memory, Text, Color, Image, IO, Hash, Parse, Serialization, Reflection, Formats, Job, Asset, Resource, Scene, World, Entity, Animation, AI, Script, Engine, FileSystem, Renderer, Material, Texture, Input, Audio, Physics, Network, Gui, Tools, and Editor.
- Added a third-party dependency policy that keeps external APIs hidden behind Cypher-owned wrappers and separates vcpkg-managed dependencies from small vendored libraries.
- Added the authoring-versus-cooked format direction for `.cymap`, `.cyscene`, `.cytex_c`, `.cymesh_c`, `.cyanim_c`, `.cybsp_c`, `.cypkg`, and related Cypher data formats.

### Changed
- Changed normal build presets to acquire only their required dependency features and added a `dependencies-all` integration preset.
- Hardened macOS dependency builds against ambient Homebrew/MacPorts include paths and mixed Apple/GNU archive tools.
- Shortened the README so it acts as a concise repository entry point instead of duplicating long-form architecture documentation.
- Updated architecture documentation to make CypherCommon the explicit public/common contract layer rather than a vague future interface layer.
- Updated project structure documentation to reflect the top-level `src/CypherCommon/` tree and the planned Common folder families.
- Updated the toolchain plan with concrete library, format, and wrapping rules for engine runtime, asset tools, and the future Mason editor.

### Verified
- Verified the complete approved dependency graph and all vendored compiled targets on Apple Silicon macOS.
- Verified the full project build with tests and benchmarks enabled and all 59 registered tests passing.

## [0.1.0] - 2026-07-04

### Added
- Added the CypherCommon Tier0 monotonic timer backend with native platform timing for Windows, Linux, and macOS.
- Added Tier0 timer tests covering initialization, frequency-based conversion, monotonic tick ordering, sleep-observed elapsed time, and stopwatch-style timer helpers.
- Added the Tier0 timer benchmark target for measuring timer tick reads and conversion overhead.
- Added Tier0 thread and system information tests to strengthen the Common foundation before higher-level runtime work.

### Changed
- Replaced the earlier inline nanosecond-only timer helpers with an explicit `Timer_Init`, `Timer_NowTicks`, `Timer_GetFrequency`, elapsed conversion, and `cy_timer_t` stopwatch API.
- Renamed the public stopwatch struct from `timer_t` to `cy_timer_t` to avoid collision with POSIX `timer_t` on Ubuntu/Linux builds.

### Fixed
- Fixed Ubuntu CI build failures caused by ambiguous `timer_t` resolution between CypherCommon and the POSIX system type.

### Verified
- Verified local CTest coverage with all registered tests passing.
- Verified the Tier0 timer benchmark builds and runs locally.

## [0.1.0] - 2026-06-04 to 2026-06-18

### Added
- Added the CypherMemory allocator foundation with arena, pool, bucket, scratch, and thread-aware allocation paths.
- Added filesystem path normalization, root normalization, path joining, basename, dirname, extension, and extension-stripping helpers.
- Added VFS directory and discovery APIs for create, delete, remove tree, rename, copy, exists, file info, directory listing, and find-file workflows.
- Added VFS mount handles, mount priority ordering, mount inspection, unmounting, and trace-resolve diagnostics.
- Added package-backed VFS reads through CypherPak integration, including package mount, unmount, package info, package file open/read/seek/tell, directory listing, find, and copy-out behavior.
- Added filesystem watch API coverage with snapshot-based polling for created, modified, and deleted loose-file changes.
- Added Windows native file-watch groundwork using `ReadDirectoryChangesW`, fixed native watch slot storage, directory handle creation, async event creation, overlapped state, watch arming, and native cleanup on unwatch.
- Added GitHub Actions CI coverage across Windows, macOS, and Ubuntu with Debug/Release builds, CTest execution, and Ubuntu sanitizer coverage.

### Changed
- Renamed common log/print usage toward shorter engine-facing macros and helpers.
- Tightened subsystem error naming so filesystem, memory, pak, render, host, command, cvar, config, and common errors remain distinct.
- Strengthened the VFS write path model so writes resolve through the configured write root while reads resolve through mounted roots and package overlays.
- Improved package/VFS overlay behavior so higher-priority package content can override loose mounted content while still falling back after unmount.
- Updated CI to use modern checkout, explicit permissions, concurrency cancellation, stricter CTest failure handling, and platform dependency setup.
- Kept native platform code behind platform macros while preserving the public VFS API as platform-neutral.

### Fixed
- Fixed cross-platform CI failures from compiler differences, Windows CRT text-mode translation, Linux dependencies, and overly large temporary filesystem watch state.
- Fixed VFS watch cleanup so active watches are unwatched before filesystem shutdown clears runtime state.
- Fixed watch flag validation so recursive watching must still specify a file or directory watch mode.
- Fixed package file handle behavior for read, seek, tell, EOF reads, permission-denied writes, and package unmount fallback.

### Verified
- Verified filesystem and package smoke tests locally.
- Verified CI passing across Windows, macOS, Ubuntu, Release/Debug, and Ubuntu ASan/UBSan jobs.

### Notes
- Native Windows watching is now created and armed, but event parsing still needs to convert `FILE_NOTIFY_INFORMATION` records into engine `watch_event_t` records.
- Linux `inotify`, macOS native watching, and async filesystem IO remain the next VFS work items.

## [0.1.0] - 2026-06-13

### Added
- Added the CypherPak package subsystem for package-backed asset storage.
- Added package-backed VFS support for mounting packages and reading, listing, finding, and copying files through the virtual filesystem.
- Added tests covering CypherPak package behavior and VFS package integration.
- Added GitHub Actions CI coverage for automated build and test verification.

## [0.1.0] - 2026-06-07

### Added
- Added the tracked future subsystem folder skeleton inspired by early CryEngine-style engine/editor/tool separation:
  - `CypherPlatform`
  - `CypherInput`
  - `CypherResource`
  - `CypherWorld`
  - `CypherEntity`
  - `CypherPhysics`
  - `CypherAudio`
  - `CypherAI`
  - `CypherAnimation`
  - `CypherNetwork`
  - `CypherScript`
  - `CypherProfile`
  - `CypherConsole`
  - `CypherEditor`
  - `CypherTools`
- Added tool placeholder folders for future asset, map, and resource compiler work:
  - `tools/CypherAssetCompiler`
  - `tools/CypherMapCompiler`
  - `tools/CypherResourceCompiler`

### Changed
- Updated the project structure documentation around the long-term CypherEngine layout.
- Updated the coding style documentation to define the Cypher module naming law:
  - `Cypher*` subsystem folders
  - `snake_case_t` data types
  - `*_desc_t`, `*_state_t`, and `*_handle_t` type conventions
  - explicit subsystem-prefixed free functions
  - future `I*` interfaces only for stable editor/runtime/tool contracts
- Updated the README to remove stale `REAP` absolute links, describe the new subsystem skeleton, and align the project identity around Cypher Software.

### Notes
- No C++ implementation files were intentionally changed as part of this architecture pass.
- Empty future subsystem directories are tracked with placeholder files until real implementation files exist.

## [0.1.0] - 2026-06-05

### Changed
- Kept public subsystem functions on the branded `CypherRender_*`, `CypherSystem_*`, `CypherCommon_*` style.
- Cleaned subsystem types back to namespace-local names such as `render::shader_t`, `render::mesh_t`, `sys::window_t`, `host::state_t`, and `common::error_t`.
- Removed the renderer-owned temporary triangle mesh/shader draw path so the renderer now only draws submitted draw items.
- Updated CypherEngine build metadata to describe the custom idTech/GoldSrc/early CryEngine-inspired runtime direction.

### Verified
- Confirmed the cleaned naming pass builds successfully with CMake using a fresh verification build directory.

## [0.1.0] - 2026-06-02

### Added
- Added the first frustum math implementation for renderer visibility work:
  - Gribb-Hartmann projection-view plane extraction
  - frustum point containment
  - frustum-vs-bounds intersection testing
- Added plane construction helpers for building planes from point/normal pairs and triangle points.

### Fixed
- Fixed frustum bounds testing to use real AABB half-extents when projecting bounds onto frustum plane normals.

## [0.1.0] - 2026-05-31

### Added
- Designed the camera frustum path around six world-space planes extracted from `projection * view`.
- Documented the renderer culling direction: object/world bounds are tested against camera frustum planes before draw submission.

## [0.1.0] - 2026-05-26

### Added
- Added `math_bounds` for AABB creation, expansion, center/size queries, point containment, and bounds overlap.
- Added `math_ray` for ray point evaluation, ray-plane intersection, and ray-bounds slab intersection.
- Added `ray_t` to the math type layer for future traces, picking, collision, and weapon-fire tests.

## [0.1.0] - 2026-05-25

### Added
- Added `math_plane` for signed plane distance and front/back/on-plane point classification.
- Added `plane_t`, `bounds_t`, and frustum-oriented math type definitions as the geometry foundation for BSP, collision, and renderer culling.

## [0.1.0] - 2026-05-24

### Added
- Added the first custom engine math library pass:
  - vector helpers
  - matrix helpers
  - quaternion helpers
  - projection/view/model transform support
- Added quaternion rotation support including construction, normalization, inverse/conjugate, multiplication, vector rotation, matrix conversion, NLerp, and Slerp.

### Changed
- Chose an OpenGL-style math convention for the renderer path:
  - column-major storage
  - column-vector transforms
  - right-handed world convention
  - `projection * view * model` transform order

## [0.1.0] - 2026-05-06

### Added
- Added SDL3 window creation and runtime event plumbing through the `sys` and `host` layers.
- Added the first OpenGL renderer backend path with SDL GL context creation, GLAD function loading, frame begin/end, and buffer clearing.
- Added shader, mesh, and renderer scaffolding for moving from test drawing toward real engine assets.

### Changed
- Moved host startup toward a cleaner orchestration path where `main` stays minimal and host owns subsystem initialization.

## [0.1.0] - 2026-05-02

### Added
- Added cleanup of the host initializaion

## [0.1.0] - 2026-04-27

### Added
- Added game/engine identity direction for `REAP`, `CypherEngine`, internal `CypherEngine`, and `Spark Software`.
- Started shaping the proper `sys_` platform layer around startup descriptors, platform/compiler identity, paths, time, sleep, and local-time services.

### Changed
- Removed bulky comments from public engine headers so API surfaces are easier to read while the engine architecture is still forming.
- Cleaned `sys_platform.h` into a compact platform API surface.
- Hid VFS runtime state inside `fs_main.cpp` instead of exposing it through public headers.

### Fixed
- Fixed cfg comment stripping so `//` comments stop parsing at the correct point instead of breaking the scan too early.
- Kept the project building cleanly after the header cleanup and VFS implementation work.

## [0.1.0] - 2026-04-26

### Added
- Added the first `fs` virtual filesystem subsystem:
  - mount table
  - virtual-to-physical path resolution
  - write path storage
  - file open/close
  - read/write
  - seek/tell
  - read-entire-file helper
- Added VFS error/type/API headers for the initial OS-file backend.
- Added docs describing the project direction:
  - `REAP` as the game/project
  - `CypherEngine` as the native engine runtime
  - `SDL3`, `OpenGL`, `Quake III BSP`, `rmdl`, and `rpk` as the long-term technical path.
- Added API documentation anchors:
  - `docs/CYPHERENGINE_API_REFERENCE.md`
  - `docs/CYPHERENGINE_API_IMPLEMENTATION.md`

### Changed
- Completed the cfg system enough to load files and dispatch parsed lines through `cfg_execute_line`.
- Updated docs and roadmap away from the earlier Raylib direction and toward the current custom runtime path.

## [0.1.0] - 2026-04-25

### Added
- Continued cfg system implementation.
- Added cfg single-line execution support for:
  - `exec`
  - `set`
  - `seta`
  - command fallback through `cmd_execute`

### Changed
- Consolidated cfg parsing so file loading delegates per-line behavior through the same execution path.

## [0.1.0] - 2026-04-24

### Added
- Added cvar mutation support through `cvar_set`.
- Started the cfg subsystem for engine/game configuration loading.
- Added early cfg file parsing and command-line interpretation work.

### Changed
- Treated cfg as the next bridge between cvars, commands, and future filesystem-backed startup config.

## [0.1.0] - 2026-04-23

### Added
- Expanded the cvar subsystem with bool parsing and typed cached values.
- Finalized the first version of `cvar_register`.

### Changed
- Improved cvar default value handling and flag validation.

## [0.1.0] - 2026-04-22

### Added
- Added the command subsystem with:
  - fixed command registry
  - command registration
  - command lookup
  - argument parsing
  - callback execution
- Added the cvar subsystem API/error foundation.
- Started cvar implementation with registry, flags, and typed storage.

### Changed
- Cleaned the command path enough for cfg/cvar integration work to build on it.

## [0.1.0] - 2026-04-21

### Added
- Added subsystem-local error enums and packed common error conversion helpers.
- Added `com_printf`, `com_dprintf`, and `com_errorf` for common output and surfaced error printing.
- Added nicer domain/error formatted output for common error reporting.
- Started command-system design and early parsing work.

### Changed
- Improved logging and error printing flow so hard surfaced errors can show subsystem domains and packed hex codes.

## [0.1.0] - 2026-04-20

### Added
- Added early `sys_` platform helpers:
  - platform detection
  - compiler detection
  - basename helper
  - monotonic time helper
  - local time helper
- Added renderer lifecycle scaffolding and then simplified it back toward a cleaner runtime contract.
- Added `sys_` and `com_` naming alignment across early engine APIs.
- Added the first changelog/documentation pass.

### Changed
- Moved foundation code into the `common` / `com_` naming path.
- Continued aligning the project around a subsystem-first architecture.

## [0.1.0] - 2026-04-19

### Added
- Added early `CypherEngine` runtime fundamentals.
- Added host/app lifecycle scaffolding.
- Started the logging subsystem:
  - log types
  - log API declarations
  - first implementation path
- Added early top-level runtime conductor work.

## [0.1.0] - 2026-04-18

### Added
- Created the CypherEngine repository foundation.
- Added initial CMake-based project scaffold.
- Added `src` and `thirdparty` layout.
- Added initial engine foundation header and baseline docs/process files.
