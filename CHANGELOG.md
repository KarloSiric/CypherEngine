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

## [Unreleased] - 2026-09-17

This integration entry records the executable work added after the September 16
development snapshot below. It distinguishes working source/compiler/runtime
paths from fields and formats that are deliberately reserved for later work.

### Added

#### Engine and format reference manual

- Added the first edition of the CypherEngine Reference Manual with a navigable
  table of contents, architecture and ownership boundaries, build and VFS
  guidance, versioning policy, complete source/cooked format catalog, current
  schema fields, limits, compiler/runtime support, diagnostics, determinism,
  security, migration rules, contributor checklists, glossary, and source index.
- Expanded the manual to edition 0.2 as a field-complete format specification:
  every implemented authored field now records its CYKV type, required state,
  default, accepted values, range, version, and cross-field rules; every frozen
  binary header, chunk, record, enum, flag, hash, order, alignment, and hard
  limit is tabulated with current compiler and runtime capability boundaries.
- Documented complete examples and exact contracts for CYKV text and tree packs,
  Tier2 schemas, project/settings/config files, CYRS, shaders, textures,
  materials, maps, input proposals, and `.cypak` archives. Reserved format names
  now state explicitly that they have no frozen fields or compatibility promise.
- Recorded implementation/specification differences instead of hiding them,
  including CYKV key and float-output drift, tree-pack identity gaps, the two
  distinct config grammars, source declarations that current render compilers
  deliberately gate, and cooked data that the current renderer does not consume.
- Added a CYKV 2 design proposal informed by Valve's published KeyValues and
  Data Model contracts. The proposal defines bounded VFS includes, explicit base
  composition, typed constants, build-context conditionals, numeric-type policy,
  schema-gated non-finite values, source provenance, semantic/provenance hashes,
  a self-identifying binary generation, and hostile-input limits while keeping
  CYKV 1 frozen.
- Added a complete proposed input-format family: source-controlled `.cyinput`,
  cooked `.cyinput_c`/`CYIN`, and writable `.cybindings`. The contract separates
  physical controls from text input and covers actions, contexts, schemes,
  keyboard/mouse/gamepad controls, chords, composites, processors, conflict
  policy, accessibility, focus loss, user migration, and deterministic runtime
  tables.
- Expanded the format catalog with gameplay-data, localization, captions,
  animation-graph, audio-event/mixer, post-processing, replay, save, mod,
  plug-in, and tool-local candidate families. Corrected the `.cypak` V10 status
  so timestamp-dependent output is not described as fully reproducible.

#### Render-asset formats and compilers

- Added V2 CYKV schemas and owned typed decoders for `.cyshader`, `.cytex`,
  and `.cymat`, while retaining exact V1 schema dispatch and compatibility
  routes.
- Added canonical semantic CYKV document hashing. Comments, whitespace, object
  member order, and equivalent numeric spelling no longer change cooker cache
  identity; schema identity and every typed value remain part of the hash.
- Added shader V2 stage objects, authored entry declarations, logical textures,
  samplers, typed parameters, defaults, scalar ranges, required flags, feature
  declarations, and bounded variant budgets.
- Advanced the current cooked shader resource from `CYSH` V2 to V3 with
  canonical logical binding records, stable 64-bit binding IDs, stage masks,
  value/resource types, array metadata, material offsets/storage sizes, an
  interface hash, `SHRF` reflection data, and canonical `SHST` names.
- Added glslang SPIR-V generation and SPIRV-Cross reflection as validation
  stages. Authored shader bindings and active linked GLSL resources must agree
  bidirectionally in name, kind, type, image dimension, array shape, and stage
  visibility.
- Added texture V2 source policy for type, semantic usage, color space, five
  alpha modes, mask cutoff, mip mode/filter/edge behavior, output intent,
  quality, streaming class, priority, and resident coarse-mip count.
- Advanced the current cooked texture resource from `CYTX` V1 to V2 with a
  128-byte metadata header, stable storage formats, independently hashed
  subresources, row/slice pitch, target profile, residency metadata, and
  canonical `mip -> frame -> layer -> face` ordering.
- Added a cooked texture contract for 1D, 2D, 3D, cube, array, frame, mip, and
  block-compressed layouts, plus exact bounded subresource lookup. The current
  compiler intentionally emits the supported uncompressed 2D subset.
- Added material V2 source data for bounded base-material inheritance, cycle
  rejection, partial overrides, `null` removals, domains, alpha/two-sided/shadow
  state, sampler presets, UV transforms, features, and Boolean/integer/float/
  vector/color/matrix values.
- Advanced the current cooked material resource from `CYMT` V1 to V2 with
  fully resolved render state, shader-interface and variant hashes, stable
  binding IDs, reflected value offsets and sizes, packed constant bytes,
  texture/UV records, and canonical strings.
- Added cross-format compilation that validates material bindings against the
  exact `CYSH` V3 interface and checks referenced `CYTX` V2 texture type,
  usage, and color-space semantics before publication.
- Added deterministic compiler identities covering source/cooked generations,
  target and build profile, dependency content, glslang and its exception ABI,
  SPIRV-Cross, image libraries, compiler implementation versions, and the pinned
  vcpkg baseline.
- Added an exceptions-enabled local glslang overlay so preprocessing, linking,
  SPIR-V generation, and diagnostics use one explicit build contract.
- Added malformed-input, compatibility, determinism, maximum-count, aliasing,
  transactional-failure, cross-format, range/type, strict-warning, sanitizer,
  and benchmark coverage for the render formats and compilers.
- Added direct benchmarks for canonical CYKV hashing, shader binding lookup by
  name and ID, texture subresource lookup, and material parameter lookup.
- Added explicit NaN/infinity EXR rejection coverage that proves a failed image
  import leaves the destination surface empty.

#### Renderer runtime and first visible draws

- Added generation- and type-checked runtime shader handles created
  synchronously from validated cooked `CYSH` views.
- Added native OpenGL vertex/fragment compilation, linked program ownership,
  bounded driver diagnostics, complete partial-failure rollback, metadata
  copying, stale-handle rejection, and shutdown cleanup.
- Added the first immutable graphics-pipeline contract with vertex-layout
  agreement, depth test/write state, back-face culling, front-face orientation,
  source-alpha blending, one exact reflected uniform block, and one optional
  sampled `sampler2D`.
- Added shader reference retention by live pipelines so native programs cannot
  be destroyed while a pipeline still depends on them.
- Added immediate backend-neutral indexed triangle submission with validation
  for frame state, object types/generations, layout equality, index and vertex
  ranges, mapped buffers, uniform-buffer size, and sampled-texture requirements.
- Added the OpenGL draw path with explicit fixed-function state application,
  uniform-block binding, sampled-image binding, and indexed submission.
- Added generation-checked immutable RGBA8 2D textures with sRGB/linear storage,
  optional mip generation, repeat/clamp addressing, linear/trilinear filtering,
  metadata queries, transactional creation, and deterministic destruction.
- Added draw-time texture lifetime/type checks and OpenGL smoke coverage for
  sampled output, uniform changes, depth clear/write restoration, and cooked
  shader program linking.
- Added `cypher_render_cube`, an offline-cooked and VFS-loaded vertical slice
  that renders a rotating lit UV-grid cube through only public System, Common,
  and Renderer APIs.
- Added deterministic finite-frame and hidden-window modes for renderer smoke
  runs, fixed-step animation, resize/minimize handling, timeouts, and shared
  cleanup after successful or partial initialization.
- Added `cypher_tile_map_preview`, which transactionally loads an authored
  `.cymap`, builds renderer-neutral boxes, draws them through the same indexed
  cube path, resolves the current cooked material/texture preview subset, and
  reloads changed map/material dependencies without discarding the last valid
  state or camera pose.
- Added `R_InitHostSurface` and a host-surface callback contract for borrowing
  an editor-owned OpenGL context, procedure resolver, framebuffer preparation,
  optional presentation, and optional present-mode changes.
- Added a live hosted-surface smoke test and a Qt `QOpenGLWidget` integration
  guide. A missing Present callback now flushes rendering and leaves composition
  to the owning framework.

#### CypherTileEditor and the `.cymap` source format

- Added `CypherTileMapCore`, a Qt-free authoring layer for bounded map
  documents, grouped edit transactions, undo/redo, dirty/saved revisions,
  validation, allocation-failure rollback, deterministic persistence, and
  renderer-neutral blockout geometry.
- Added operational `cypher.map` source schemas V1, V2, and V3. The reader
  accepts all three versions and the writer emits V3.
- Added V1 map identity, dimensions, physical cell/level metrics, sparse active
  cells, floor elevation, wall height, material slots, flags, player spawn, and
  stable door markers.
- Added V2 flat/cardinal stair shapes and bounded tread counts.
- Added V3 stable numeric material-slot bindings to canonical `.cymat` paths.
- Added deterministic row-major cell output, stable marker/material ordering,
  sparse empty-cell omission, complete temporary-state validation, older-version
  compatibility, and transactional failure behavior.
- Added generated floor, exposed-boundary wall, cliff, door, and stair boxes.
  Authored documents remain the source of truth and generated geometry remains
  derived data.
- Added Select, Paint, Erase, Rectangle, Line, Fill, Spawn, Door, Pan, and
  eyedropper tools plus grouped strokes and room/piece stamps.
- Added room, corridor, corner, junction, stair, door, and boundary-height piece
  presets with rotation and one-transaction stamping.
- Added exact sparse multi-selection shared by Top, Front, Side, 3D, object tree,
  and property inspectors.
- Added transactional move, duplicate, rotate, elevate, wall-height, delete,
  select-all, floor-presence, material, shape, and stair-count operations with
  collision, capacity, arithmetic, and bounds validation before mutation.
- Added map-property editing for dimensions, cell size, and level height with
  undo/redo and shrink rejection when authored content would be lost.
- Added Top XY, Front XZ, Side YZ, and embedded 3D panes; duplicate orthographic
  views; pane relocation; maximize/restore; persisted order/sizes/visibility;
  per-pane controls; and shared selection highlighting.
- Added adaptive zero-anchored grids, coordinate rulers, view metrics, material
  identification, depth cues, configurable internal seams, and semantic X/Y/Z
  axes.
- Added fly/orbit camera navigation, RMB look, WASD/QE movement, speed modifiers,
  pan, pointer-anchored zoom, selection/map framing, player-spawn positioning,
  exact direction presets, level movement, auto-orbit, and session bookmarks.
- Added Front/Side fixed construction layers and single-click 3D painting,
  erasing, marker placement, eyedropper behavior, geometry picking, and floor
  plane construction inside map bounds.
- Added an embedded `QOpenGLWidget` CypherRender viewport that rebuilds from
  in-memory edits, throttles continuous stroke updates, and preserves the Qt
  context/framebuffer ownership boundary.
- Added project material discovery, dependency cooking, map-slot binding,
  clearing, refresh, application to selected cells, cooked base-color preview,
  tint/UV transforms, shared texture caches, bounded memory, and diagnostic
  fallback for damaged or missing resources.
- Added textured previews in Top, Front, Side, embedded 3D, and the external
  runtime map preview, with transactional refresh and live dependency reload.
- Added a command console, history, completion, validation/build/run commands,
  and an asynchronous local shell with streamed stdout/stderr, working-directory
  control, stop/restart/clear/copy, and nonblocking process ownership.
- Added Materials & Pieces, All Objects, Properties, and Console docks; object
  filtering; synchronized multiselection; configurable layout; eleven color
  themes; bundled offline SVG resources; and recorded icon licenses/provenance.
- Added readable atomic `editor.ini` configuration plus native `QSettings`
  state, import/export profiles, live Apply/OK/Cancel/Restore Defaults behavior,
  camera/canvas/workspace/map-default settings, custom shortcuts, and
  shortcut-conflict validation.
- Added focused core, serialization, material, camera, navigation, selection,
  region, workspace, settings, console, shell, hosted-renderer, and live-preview
  test coverage.

#### Runtime foundations, startup, build, assets, and documentation

- Added dedicated Command, CVar, Config, Memory pool/arena/scratch/thread/runtime,
  FileSystem, Pak, Log, Resource, System, and Host-focused regression coverage.
- Added ordered and shuffled pool benchmarks plus arena write/clear/reset and
  expanded Resource/render-format benchmarks.
- Expanded the live Host startup report into product/build, System/process,
  seven-arena Memory, core services, FileSystem, Command, CVar, Config, Log,
  runtime policy, SDL/window/display, startup I/O, timing, and final READY
  sections.
- Added read-only Command/CVar enumeration and copied platform/backend identity
  queries so Host diagnostics report owned subsystem state through public APIs.
- Added Debug and Release targets/presets for the renderer examples,
  `CypherTileMapCore`, Tile Editor GUI, and focused editor tests.
- Added automatic shader/material/texture cooking for renderer and editor
  examples and optional Qt 6 Core, Gui, Widgets, OpenGLWidgets, and Svg wiring.
- Added authored cube and tile-surface GLSL recipes, brick/grid/hazard
  texture/material recipes, three deterministic 128x128 RGBA development
  textures, and V1/V2/V3 map examples.
- Added the non-renderer validation report and work log, renderer first-draw
  guide, host-surface embedding guide, six-month execution plan, Tile Editor
  reference/navigation research, and expanded format/catalog/schema documents.
- Updated the project status, documentation index, tool inventory, clangd
  configuration, dependency manifest, and format maturity descriptions around
  the executable state of the repository.

### Changed

- Canonical CYKV hashing now ignores authoring-only ordering and formatting while
  preserving schema identity and all semantic values used by cooker caches.
- Texture mip generation now filters sRGB in linear space, filters straight
  alpha through premultiplied color, renormalizes RGBA8/RGBA32F normal vectors,
  and uses exact area weights for odd dimensions.
- Shader and material constant records now share one canonical name-sorted ABI,
  including padded `vec3` storage and three padded `mat3` columns.
- Render resource versions advanced from `CYSH/CYTX/CYMT = 2/1/1` to
  `3/2/2`; explicit compatibility readers remain responsible for older files.
- The format catalog now distinguishes the implemented Tile Editor `.cymap`
  V1-V3 source format from the still-planned `.cymap_c` and World runtime.
- The project resume status now records the completed standalone shader,
  pipeline, draw, texture, cube, hosted-surface, and Tile Editor slices while
  retaining the real Host/World integration gap.
- Generated cooked texture, material, map, and mesh outputs now join cooked
  shaders in the source-control ignore policy.
- Runtime startup INFO output now uses fixed named sections, aligned hierarchy,
  wrapped descriptions, and a distinct READY block. Routine INFO lines omit
  repetitive prefixes while warnings/errors remain explicit and file sinks keep
  detailed metadata.
- The current Tile Editor map remains a bounded blockout document with one floor
  surface per X/Y cell; arbitrary mesh/brush topology and stacked floors remain
  outside this source format.

### Fixed

- Rejected aliased `CYRS` input/output ranges transactionally and applied
  shader binding limits before range arithmetic or caller-memory traversal.
- Fixed arena clear-before-decommit behavior, stale diagnostics, trace indexing,
  scratch error retention, and allocator-wrapper synchronization.
- Fixed asynchronous FileSystem cancellation publishing completion while a
  worker still borrowed caller memory, shutdown admitting new work, and live
  native/package handles being silently replaced during reopen.
- Hardened Pak bounds, path, compression, truncation, payload-hash, reader/writer
  ownership, temporary publication, replacement failure, and staging cleanup.
- Fixed Command/Config/CVar truncation, recursive config bounds, trailing
  arguments, embedded-NUL input, overlapping CVar source text, full-token numeric
  parsing, and modified-state restoration.
- Fixed macOS available-memory reporting with overflow-safe Mach VM accounting.
- Fixed Log timestamp handling, producer and formatter truncation reporting,
  configuration races, failed sink replacement, early file truncation, and
  concurrent writes/reconfiguration/shutdown.
- Fixed renderer shader, pipeline, draw, and texture failure paths so native
  objects and retained references roll back before a public handle is published.
- Fixed editor operations so allocation failures, invalid transforms, malformed
  map/material dependencies, interrupted gestures, and failed reloads preserve
  the previous document, selection, history, preview, and saved revision.

### Verified

- Retained the independently completed non-renderer result: 232 selected CTest
  entries passed in Debug and ASan/UBSan; allocator-wrapper, Log, and FileSystem
  suites passed under ThreadSanitizer.
- Retained the runtime pool result: eight cases and 58,552 assertions passed in
  Debug and ASan/UBSan; four Release pool workloads ran for five repetitions.
- Retained the Release non-renderer baseline: 33 workloads across five
  executables ran for five repetitions without reported workload errors.
- Added focused format/compiler evidence for canonical hashing, `CYSH` V3,
  `CYTX` V2, `CYMT` V2, reflection agreement, inherited material resolution,
  semantic texture validation, malformed input, compatibility, and lookup paths.
- Added renderer contract/native smoke coverage for shader ownership, compile/
  link rollback, pipeline dependencies, indexed validation, sampled textures,
  hosted contexts, uniform changes, and framebuffer pixel results.
- Reconfigured and rebuilt the complete `tile-editor-debug` graph after the
  dependency, format, renderer, and editor changes, then passed all 272
  registered CTest entries. The 25.45-second final run included native OpenGL
  smoke tests, the 182-case Tile Editor workspace executable, map/material
  previews, all three render-asset compilers, and ResourceCompiler process and
  shader-corpus integration tests.
- Built the six changed Release benchmark targets and smoke-ran all 44 registered
  cases across `CYSH`, `CYTX`, `CYMT`, canonical CYKV hashing, Memory, and the
  Resource runtime. This run verifies executable benchmark coverage; it is not
  recorded as a comparative performance baseline.

### Known limitations

- The real `CypherEngine` Host does not yet run the standalone renderer draw
  path or own an executable World submission loop.
- Renderer native work currently targets the OpenGL 4.1 desktop baseline and a
  singleton frontend with one standalone or hosted surface.
- The first pipeline supports triangle lists, one optional uniform block, and one
  optional `sampler2D`; command recording, render graphs, render targets,
  instancing, base vertex, primitive restart, mesh-resource loading, generalized
  material binding, and shader hot reload remain future work.
- Shader variants, alternate entries, independent samplers, and unsupported
  resource shapes remain compiler errors until their cooked/runtime contracts
  exist.
- Texture cooking currently emits uncompressed 2D RGBA8/RGBA32F. DDS/KTX2
  preservation, compressed output, advanced filters/edge policies, coverage
  preservation, and RGB dilation remain gated.
- Material features and `.cysurface` remain gated, and the renderer preview
  consumes only the bounded base-color/tint/UV subset.
- `.cymap` is an operational editor source format. There is no production
  `.cymap_c`, runtime World loader, visibility/navigation/collision partition,
  arbitrary brush/mesh editor, or general entity serialization yet.
- Tile maps retain one floor surface per X/Y cell; walls derive from cell
  boundaries and stairs remain straight cardinal pieces.
- Current checked-in shader, texture, and material recipes use schema V1 and
  exercise compatibility routes. V2 end-to-end coverage currently comes mainly
  from focused compiler fixtures.
- CypherPak payload compression, compressed indexes, archive signatures, and
  crash-durable publication remain unimplemented.

## [Development snapshot] - 2026-09-16

This development snapshot consolidates notable repository work from roughly
2026-08-07 through 2026-09-15. Planned work is intentionally excluded.

### Added
- Added a sectioned live startup manifest covering product/build identity,
  System/process state, all seven memory arenas, FileSystem policy and mounts,
  Command and CVar registries, config sources, Log sinks and routing, runtime
  policy, SDL/window/display state, startup I/O, and per-stage timings.
- Added read-only Command/CVar registry enumeration and copied SDL backend
  identity queries so Host diagnostics report registered and platform-owned state
  without reaching into subsystem internals.
- Added a dedicated runtime `CypherMemory` pool test target covering borrowed
  external/arena storage, alignment, exhaustion/reuse, rejected operations,
  overflow, counters/reset, zero allocation, and deterministic payload checks
  across allocation-bitmap boundaries.
- Added pool benchmarks for shuffled free order at 64, 1,024, and 16,384 slots,
  and failure reporting for both ordered and shuffled pool workloads.
- Added the backend-neutral renderer frontend and OpenGL implementation for
  lifecycle, capability discovery, clear/present, resize, presentation policy,
  and explicit System-owned native window/context integration boundaries.
- Added generation- and type-checked renderer buffers with OpenGL storage,
  update, map/unmap, query, destruction, shutdown cleanup, tests, and benchmarks.
- Added vertex-format and layout validation plus generation-checked vertex-input
  objects backed by OpenGL vertex arrays, including retained buffer ownership,
  stale/wrong-type handle coverage, smoke tests, and benchmarks.
- Added focused `CypherSystem` runtime, window, event-queue, OpenGL-context, death
  helper, and benchmark coverage around the rebuilt renderer platform boundary.
- Added a first-party image-processing layer covering formats, owned surfaces,
  borrowed views, codecs, conversion, resize, mip generation, processing,
  tests, and Release benchmarks.
- Added the initial Picasso texture/material authoring core and Qt application,
  including channel and texture-set models, paint operations, canvas/console/
  workspace UI, licensed icon assets, and focused core/console/document tests.
  This is an initial tool slice, not a completed production editor.
- Added explicit build targets and presets for verified Common, image, resource,
  render-format, tool-framework, ResourceCompiler, render-asset compiler, and
  Picasso components.
- Added the World/renderer ownership ADR, detailed Host/Renderer and World module
  maps, and reserved `CypherWorld` public/private headers and responsibility
  documents. World remains contract-only; no runtime World operations are
  claimed.
- Added an evidence-backed six-month execution plan covering current subsystem
  status, vertical slices, dependencies, renderer order, measurable repository
  size, estimate uncertainty, monthly gates, risks, and explicit deferrals.
- Added focused `CypherLog` tests for lifecycle state, synchronized configuration
  snapshots, failed sink replacement, concurrent writers, and concurrent runtime
  configuration updates.
- Declared the next `CypherSystem` lifecycle and emergency-output boundary,
  including cooperative quit requests, final process termination, raw bootstrap
  diagnostics, and fatal diagnostics. Implementations remain the next System task.
- Added the first `CypherShaderCompiler` vertical slice: CYKV recipe decoding,
  exact schema and semantic validation, deterministic define application,
  glslang preprocessing/parsing/cross-stage linking, dependency fingerprints,
  structured diagnostics, and canonical `.cyshader_c` output.
- Added `CypherResourceCompiler` 1.0.0 with modular compiler dispatch,
  compile/validate and registry-backed compiler/format discovery commands,
  a shared read-only VFS provider, repeatable inputs, directory and wildcard
  discovery, response files, generated zsh completion, stable exit classes,
  text/NDJSON records, fixed-width aggregate progress, structured completion
  summaries, cancellation, branded descriptor-driven help, and terminal-aware
  ANSI color output.
- Added shared transactional artifact publication and native recursive-directory,
  remove, and atomic-replace helpers so failed cook operations do not damage a
  previously published resource.
- Added process-level ResourceCompiler coverage for product identity, command
  help, version output, compiler/format discovery, dry-run validation, progress
  rendering, VFS directory and wildcard discovery, repeatable inputs, generated
  completion, exact schema locations, aggregate summaries, output tree creation,
  deterministic repeated cooking, clean JSON records, forced ANSI output,
  invalid GLSL failure, and temporary-file cleanup.
- Added the host-neutral `CypherToolFramework` foundation for tool application,
  command, option, invocation, diagnostic, progress, event, dependency, artifact,
  report, document, workspace, session, compiler, and input-set contracts.
- Added reusable compiler registration and dispatch with explicit ambiguity
  rejection, validated execution reports, and synchronous host callbacks shared
  by command-line tools, Mason, automation, and tests.
- Added portable CLI argument parsing, generated help, owned recursive response
  files, terminal capability detection, structured text/JSON display, Ctrl+C
  cancellation, report serialization, and process exit-code orchestration.
- Added explicit source roots for 58 planned Cypher tool products under
  `src/CypherTools`, with product targets registered manually only when their
  implementation and tests exist.
- Added shared Tier2 semantic validation for stable identifiers, ASCII member
  names, canonical virtual paths, and typed resource-reference extensions.
- Added schema support for bounded dynamic object maps and latest-version lookup
  for tooling discovery while preserving exact-version runtime validation.
- Added version 1 CYKV source schemas and typed zero-copy decoders for shader,
  texture, and material renderer assets.
- Added the `CYRS` version 1 cooked-resource envelope with explicit little-endian
  serialization, ordered chunk descriptors, per-chunk codec/alignment metadata,
  content fingerprints, and malformed-layout rejection.
- Added the `CYSH` version 1 cooked shader contract with deterministic `SHMD` and
  `SHCD` chunks, canonical graphics/compute stage sets, strict UTF-8 GLSL
  validation, borrowed stage views, and whole-file/chunk hash verification.
- Added renderer format and cooked envelope tests covering defaults, dynamic
  bindings, canonical references, numeric material values, binary round trips,
  truncation, overlap, alignment, codecs, hashes, and transactional failures.
- Added cooked shader tests covering round trips, deterministic output,
  malformed code, invalid stage sets, noncanonical layouts, damaged hashes, and
  transactional reads.
- Added cooked shader write/read benchmarks for representative 4 KiB and 256 KiB
  GLSL stages to establish cooker and load-time baselines.
- Added renderer format documentation and a maturity catalog that separates
  implemented contracts from provisional future formats.
- Added the first synchronous `CypherResource` runtime, including loader-type
  registration, stable resource identity, generation-safe handles, reference
  counting, cache lookup, failed-load rollback, dependency-cycle detection,
  reverse-load-order shutdown, diagnostics, tests, and Release benchmarks.
- Added a dedicated `Cypher::ResourceRuntime` CMake target and linked the engine,
  resource tests, and resource benchmarks through the target boundary.
- Added the mesh-first Mason map-authoring direction, separating editable mesh
  topology from generic CypherMath operations and treating BSP/CSG as optional
  compiler techniques rather than the persistent authoring representation.
- Added the CypherSecurity primitive layer backed by libsodium, including guarded
  secret memory, cryptographic randomness, BLAKE2b digests and KDF, SipHash,
  Argon2id password records, XChaCha20-Poly1305 AEAD, Ed25519 signatures, X25519
  directional key exchange, authenticated secret streams, and strict Hex/Base64.
- Added security tests for standard vectors, domain separation, nonce exhaustion,
  canonical encodings, tampering, stream ordering, invalid state, capacity limits,
  guarded-key ownership, rejected peer keys, and concurrent read-only key use.
- Added Release benchmarks for guarded memory, key derivation, authenticated
  encryption, signatures, key exchange, secret streams, and text encodings.
- Added `docs/security_model.md` to define algorithm choices, key and nonce
  lifetimes, authentication rules, failure behavior, and boundaries with network,
  package, tooling, and anti-cheat systems.
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
- Reworked the live startup presentation into fixed-width named sections with
  aligned parent/child fields, wrapped descriptions, readable memory-arena rows,
  and a distinct final READY block while retaining the complete diagnostic
  inventory.
- Added a console log format that prints routine INFO records without repetitive
  severity/channel prefixes or ANSI color wrappers; warnings and errors remain
  visibly prefixed and colored, while file sinks keep detailed metadata.
- Replaced the previous ad-hoc renderer shader, mesh, camera, and draw path with
  an explicit frontend/backend architecture. The new foundation is intentionally
  not yet feature-equivalent: runtime shader programs, pipelines, and public
  draws remain the next milestones.
- Split System platform services into target-specific Windows, POSIX, macOS, and
  Linux translation units and consolidated compile-time target queries in the
  Common platform contract.
- Standardized runtime entry-point naming around `Sys_`, `FS_`, `Log_`, `Cmd_`,
  `Cvar_`, `Cfg_`, `Mem_`, `Res_`, `Host_`, `R_`, and backend `GL_` prefixes.
- Strengthened ResourceCompiler output handling and reporting while reusing the
  shared texture, material, VFS, and transactional-publication contracts.
- Updated current-status and documentation navigation to identify runtime shader
  programs from cooked `CYSH` resources as the active milestone, followed by
  pipeline/draw/first cube and then reflection/material bindings.
- Made `CypherLog` runtime state, configuration, sink handles, and writes mutually
  exclusive so renderer, resource, networking, and worker threads can log without
  racing reconfiguration or shutdown.
- Changed `Log_GetConfig()` to return a synchronized value snapshot instead of a
  reference to mutable global logger state.
- Made logger sink reconfiguration transactional: replacement handles are opened
  first and committed only after every requested sink succeeds.
- Guarded Linux, macOS, POSIX, and Win32 System translation units before including
  target-native headers so clangd can index the complete source tree on one host.
- Replaced several Tier0/Tier1 umbrella includes with the direct declarations
  required by allocator, character, error, and SoA implementation units.
- Clarified ToolFramework naming so terminal-specific behavior uses `ToolCli*`,
  while compiler, input, report, and host contracts remain frontend-neutral.
- Changed runtime resource handles to an opaque 64-bit `16/32/16`
  slot/generation/type layout, allowing 65,536 manager slots while making stale
  handle aliasing substantially less likely during long editor sessions.
- Changed the resource lookup removal path to repair linear-probe clusters
  without accumulating tombstones during repeated load/unload workflows.
- Changed the clangd database helper to link `build-clangd/compile_commands.json`
  to the active CMake Debug database, with an atomic-copy fallback for platforms
  where developer-created symbolic links are unavailable.
- Changed normal build presets to acquire only their required dependency features and added a `dependencies-all` integration preset.
- Hardened macOS dependency builds against ambient Homebrew/MacPorts include paths and mixed Apple/GNU archive tools.
- Shortened the README so it acts as a concise repository entry point instead of duplicating long-form architecture documentation.
- Updated architecture documentation to make CypherCommon the explicit public/common contract layer rather than a vague future interface layer.
- Updated project structure documentation to reflect the top-level `src/CypherCommon/` tree and the planned Common folder families.
- Updated the toolchain plan with concrete library, format, and wrapping rules for engine runtime, asset tools, and the future Mason editor.

### Fixed
- Fixed macOS physical-memory diagnostics returning zero available bytes by
  querying Mach VM statistics with overflow-safe page accounting.
- Fixed CVar float caching accepting a valid numeric prefix of ordinary text,
  including `info` as infinity, and made the modified flag clear when a value is
  restored to its registered default.
- Fixed optional config loading suppressing every open error; only a genuinely
  missing optional path is now skipped.
- Fixed compact Log sinks ignoring their timestamp setting, made compact and
  detailed formatters reject truncated output records, and mark messages that
  exceed the logger's owned record buffer instead of silently shortening them.
- Fixed runtime arena clearing before virtual decommit, stale reset diagnostics,
  allocation-trace indexing, and scratch-scope error retention.
- Fixed allocator-wrapper races between allocation/free, binding changes, and
  diagnostic reads; operations now return the result protected by their own lock.
- Fixed FileSystem cancellation reporting a terminal result before the read
  worker released its caller buffer, shutdown admitting new asynchronous work
  while draining, and reopening an owned file discarding its handle.
- Hardened Pak entry bounds/compression/path validation and reader/writer reopen
  ownership; archive output now uses an exclusive temporary sibling and replaces
  the destination only after successful writing and flushing.
- Fixed silent Command/Config/CVar truncation, unchecked nested config execution,
  trailing config arguments, embedded-NUL config files, overlapping CVar input,
  and out-of-range numeric parsing through `atoi`/`atof`.
- Fixed Log candidate preparation truncating existing files before a later sink
  failed to open; added configuration validation before file operations. Final
  truncation across multiple files remains explicitly non-atomic.
- Fixed logger data races between record emission, configuration reads, sink
  replacement, initialization, and shutdown.
- Fixed failed logger reconfiguration so an invalid replacement sink no longer
  closes or partially replaces the active sink set.
- Fixed the `CypherSystem` declaration/definition exception-specification mismatch
  introduced while drafting the lifecycle contract, and restored the public
  `Sys_IsInitialized()` declaration.

### Verified
- Verified the final non-renderer pass on 2026-09-16: all 232 selected CTest
  entries pass in Debug and ASan/UBSan, and the three ThreadSanitizer suites
  (allocator wrappers, Log, FileSystem) pass. Ran 33 Release workloads across
  five executables with five repetitions each and no workload errors. See the
  [runtime work log](docs/non_renderer_work_log.md) for scope, reproduction,
  measured timings, and remaining platform/contract limits.
- Verified the dedicated runtime pool target in Debug and ASan/UBSan on
  2026-09-16: eight cases and 58,552 assertions pass in each configuration.
  Built and ran four Release pool workloads for five repetitions each; results,
  commands, and measurement limits are recorded in
  [the non-renderer validation report](docs/non_renderer_validation_2026-09-16.md).
- Historical validation evidence: the existing Debug CTest log records all 235
  registered tests passing on Apple Silicon macOS on 2026-09-15. This is the
  pre-change baseline, not a fresh full-suite run of the 2026-09-16 changes.
- Verified the complete Debug build and all 223 registered tests on Apple Silicon
  macOS after the logger synchronization and direct-include changes.
- Repeated the concurrent `CypherLog` suite 25 consecutive times without failure
  and verified the focused logger target under ASan/UBSan.
- Verified the complete shader-tools Debug and Release builds with all 219
  registered tests passing on Apple Silicon macOS, including the VFS,
  ResourceCompiler process integration, and recursive 100-shader corpus paths.
- Verified the focused ToolFramework, shader compiler, and ResourceCompiler
  eight-test set in Release and under ASan/UBSan.
- Verified the Debug engine build with all 210 registered tests passing and the
  Release renderer-format target.
- Verified the ToolFramework core, CLI, response-file, and compiler suites under
  Debug and ASan/UBSan, including nested response files, option precedence,
  compiler ambiguity, report validation, and saturating session identifiers.
- Verified the cooked shader reader/writer under ASan/UBSan and strict Clang
  warnings, with Release throughput around 1.2-1.35 GiB/s on the local Apple
  Silicon benchmark host for representative GLSL payloads.
- Verified the resource runtime and packed-handle tests under ASan/UBSan.
- Verified strict warning compilation for the resource implementation and stable
  Release benchmark results for payload lookup, cached acquisition, and
  synchronous load/unload bookkeeping.
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
