<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/FORMAT_CATALOG.md
//  Purpose: Tracks Cypher source and cooked format maturity.
//  Details: The catalog distinguishes implemented contracts from active compiler
//           integration and reserved names so plans are not mistaken for support.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//  - Added render-asset version and compatibility status on 2026-09-17
//  - Added input, gameplay-data, presentation, user-state, and generated-record
//    candidates alongside the reference manual on 2026-09-17
//  - Accepted the CYKV 2 contract and CYDF generic profile on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Format Catalog

## Maturity Labels

| Label | Meaning |
| --- | --- |
| Implemented | The named parser, schema, reader, writer, or compiler route exists and is tested. |
| Partial | A real working slice exists, but one or more documented end-to-end layers remain unavailable. |
| Active | A frozen lower-level contract exists and the next integration layer is being implemented. |
| Specified | The normative identity and behavior are accepted, while some or all implementation layers remain unavailable. |
| Planned | Purpose and provisional name are recorded; the source or binary layout is not frozen. |
| Proposal | Responsibility and candidate identity are under review and may change. |

A format can have different maturity at each layer. The detailed source syntax,
binary layouts, limits, and deliberate compiler gates live in
[Renderer Asset Contracts](RENDER_ASSETS.md). The complete cross-subsystem
inventory, admission policy, and compatibility guidance live in the
[CypherEngine Reference Manual](../CYPHERENGINE_REFERENCE_MANUAL.md).
Language and generic-profile details live in [CYKV 1](CYKV.md),
[CYKV 2](CYKV_2.md), and [CYDF](CYDF.md).

## Foundation And Configuration

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Generic structured data language | CYKV text V1 and V2 | CYKV binary pack where useful | V1 implemented; V2 Tier1 definitions/includes/bases, resolver, resolved writer, and canonical hash implemented; Tier2 schemas, compilers, provenance, and manifests pending |
| Generic schema-selected document profile | `.cydf`, encoded as CYKV | Schema-owned cooked resource when justified; no universal CYDF binary | Specified; dedicated dispatch, schemas, compilers, and consumers not implemented |
| Project manifest | `.cyproject`, `cypher.project` V3 ([spec](CYPROJECT.md)) | None | V1 and V2 decoders implemented; V3 specified; application integration pending |
| Workspace | `.cyworkspace`, `cypher.workspace` V2 ([spec](CYWORKSPACE.md)) | None | V1 decoder, creation, and scope stack implemented in EditorCore; V2 session and per-map state specified; never packaged |
| Map source | `.cymap` + `.cymapchunk`, `cypher.map` V10 ([spec](CYMAP.md)) | `.cymap_c` via the map compiler (planned; ADR 0009) | Implemented in the Map workspace core: readable geometry codec (brushes, meshes, patches, terrains), nested brush entities, shapes, foliage, notes, groups, prefabs, info annotations, compact layout, hand-edit identity rules, damage handling, disk load and staged save of the chunk folder; example map under `docs/formats/examples/cymap` verified byte for byte by the tests |
| Editor theme, keymap, layout | `.cytheme` V2, `.cykeymap` V2, `.cylayout` V2 ([specs](EDITOR_FORMATS.md)) | None | V1 and V2 implemented in EditorCore (V1 files still read, saves write V2); the built-in theme and keymap are complete catalogues checked by tests |
| Game profile, plugin manifest | `.cygame` V1, `.cyplugin` V1 ([specs](EDITOR_FORMATS.md)) | None | Specified; decoders not implemented |
| Settings | `.cysettings`, `cypher.settings` V2 | None | Tolerant V1/V2 decoding, settings store, scope resolution, and safe file policy implemented |
| Command/CVar script | `.cfg` / `.cycfg` | None | Implemented runtime family |
| Generic cooked resource | N/A | `CYRS` container V1 | Implemented |
| Self-hosted schema | `.cyschema` | Compiled schema/registry data | Proposal; current schema descriptors remain C++ owned |
| Schema-selected gameplay data | `.cydf` with exact domain schema | Schema-owned cooked resource when justified | Specified CYDF use; `.cydata` and `.cydata_c` proposals withdrawn; first gameplay schema and consumer not implemented |

## Input And User Controls

| Purpose | Source/runtime | Cooked | Status |
| --- | --- | --- | --- |
| Project action maps and defaults | `.cyinput`, proposed `cypher.input` V1 | `.cyinput_c`, proposed `CYIN` V1 in CYRS | Proposal; System keyboard/text/mouse events exist, Input runtime/compiler do not |
| User binding overrides | `.cybindings`, proposed `cypher.input_bindings` V1 | None | Proposal; writable sparse overrides, never packaged |

The input family is documented in [Input Actions And Bindings](INPUT_ACTIONS.md).
It uses action terminology because keyboard, mouse, wheel, gamepad, chords,
composites, contexts, processors, and accessibility exceed a keyboard key map.

## Renderer Vertical Slice

| Purpose | Source contract | Cooked contract | Compiler status | Runtime status |
| --- | --- | --- | --- | --- |
| Shader recipe | `.cyshader`: schemas V1 and V2 | `.cyshader_c`: `CYSH` V2 read/write and V3 current | V1 and V2 implemented; V2 active GLSL resources are proven against authored texture/parameter interfaces through glslang SPIR-V and SPIRV-Cross; variants, alternate entries, and independent samplers are gated | Validated resource views and generation-checked OpenGL program creation implemented; other backends and hot reload remain deferred |
| Texture recipe | `.cytex`: schemas V1 and V2 | `.cytex_c`: `CYTX` V1 read compatibility and V2 current | V1 and V2 implemented; preserved containers and advanced filtering policies gated | Validated subresource views and the first immutable RGBA8 2D upload/binding path are implemented; general CYTX upload, compression, and streaming remain deferred |
| Material | `.cymat`: schemas V1 and V2 | `.cymat_c`: `CYMT` V1 and V2 read/write | V1 and V2 implemented; feature variants, surfaces, and independent samplers gated | Validated V1/V2 views and a bounded base-color preview path are implemented; general renderer material binding remains deferred |
| Surface definition | `.cysurface` reserved by material V2 | `.cysurface_c` | Planned | Planned |
| Mesh recipe | `.cymesh` | `.cymesh_c` | Planned | Planned |

### Renderer Compatibility Summary

| Family | Frozen compatibility input | Current source | Current cooked identity |
| --- | --- | --- | --- |
| Shader | `.cyshader` V1 -> `CYSH` V2 | `.cyshader` V2 | `CYSH` V3 |
| Texture | `.cytex` V1; `CYTX` V1 read | `.cytex` V2 | `CYTX` V2 |
| Material | `.cymat` V1 -> `CYMT` V1 | `.cymat` V2 | `CYMT` V2 |

Unknown source schema and cooked resource versions are rejected. Compatibility
is an explicit decoder/reader path; byte-shape guessing and silent reinterpretation
are forbidden.

## World And Simulation

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Map | `.cymap`: `cypher.map` V10 and later; root document plus chunk documents ([spec](CYMAP.md)) | `.cymap_c` | Source implemented (see the Map source row above); cooked layout planned. `cypher.map` V1-V3 are the legacy tile-map lineage, read only by TileEditor's import |
| Tile map | `.cytilemap`: `cypher.tilemap` V1 | `.cytilemap_c` only if a tile runtime product requires it | Authored source, deterministic persistence, validation, generated blockout geometry, and preview implemented in CypherTileEditor |
| Prefab/entity template | `.cyprefab`, `cypher.prefab` V1 ([spec](CYMAP.md), section 6.12) | `.cyprefab_c` | Specified; decoder not implemented |
| Physics setup | `.cyphys` | `.cyphys_c` | Planned |
| Navigation | `.cynav` | `.cynav_c` | Planned |
| Mission/logic graph | `.cyflow` | `.cyflow_c` | Planned |

## Character And Presentation

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Skeleton | `.cyskel` | `.cyskel_c` | Planned |
| Animation clip | `.cyanim` | `.cyanim_c` | Planned |
| Animation graph/evaluator | `.cyanimgraph` | `.cyanimgraph_c` | Proposal; separate from clip/sample data |
| Particle system | `.cyparticle` | `.cyparticle_c` | Planned |
| Sound sample/stream recipe | `.cysnd` | `.cysnd_c` | Planned |
| Audio event/rule stack | `.cyaudioevent` provisional | Cooked event resource | Proposal; exact name not frozen |
| Audio mixer/bus graph | `.cymix` provisional | Cooked mixer graph | Proposal; exact name not frozen |
| Font family | `.cyfont`, `cypher.font` V2 ([spec](CYFONT.md)) | `.cyfont_c` | V1 source decoder implemented (faces, fallbacks); V2 rendering, glyph, and metric members specified; cooked atlas planned |
| Localization catalog | `.cyloc` | `.cyloc_c` | Proposal |
| Captions/subtitles | `.cycaption` | `.cycaption_c` | Proposal |
| UI layout/style | `.cyui` | `.cyui_c` | Planned |
| Post-processing profile | `.cypostfx` | `.cypostfx_c` | Proposal |
| Cinematic sequence | `.cycine` | `.cycine_c` | Planned |

## Distribution

| Purpose | Format | Status |
| --- | --- | --- |
| Package archive | `.cypak` | V10 reader/writer and FileSystem mount implemented with sorted uncompressed payloads and per-file hashes; timestamp serialization currently prevents a full reproducibility claim; compression, archive hashes/signatures, and a formal external specification remain future work |
| Resource/build/release manifests | `.cymanifest` with exact schema IDs | Planned; resource, preload, package, and release responsibilities must remain distinct |
| Mod/add-on metadata | `.cymod` | Proposal |
| Plug-in/module metadata | `.cyplugin` V1 ([spec](CYPLUGIN.md)) | Specified; decoder not implemented |
| Derived-data cache | Internal | Planned |

## Generated Runtime Records And Tool-Local State

| Purpose | Format | Status |
| --- | --- | --- |
| Replay/demo | `.cyreplay`, proposed `CYRP` | Proposal; generated versioned runtime record, not an authored/cooked pair |
| Save/checkpoint/profile | `.cysave`, proposed `CYSV` | Proposal; generated writable record with migration and backup policy |
| Editor theme | `.cytheme`, `cypher.theme` V2 | Token registry, base chains, derived colours and `"auto"`, choices, resolution, editing, and audit implemented in EditorCore; the framework catalogue (138 tokens) is registered by EditorGui; TileEditor's INI themes to be migrated |
| Editor keymap | `.cykeymap`, `cypher.editor_keymap` V2 | Chords, held keys, mouse gestures, platform overlays, context stacks, lookup, conflicts, and editing implemented in EditorCore; editor shortcuts only, game input is `.cyinput`/`.cybindings` |
| Editor dock layout | `.cylayout`, `cypher.layout` V2 | Decoder, builders, and encoder implemented in EditorCore, including window placement, closed panels, toolbars, status bar, and the view grid; shown through the Qt Advanced Docking System |
| Per-map editor state | Workspace state (ADR 0009) | Planned; kept in the `.cyworkspace`, not beside the map |
| Recovery journal/autosave | Internal | Planned tool-operational format |
| Asset/dependency/cook database | Internal, likely query-oriented storage | Planned; no public `.cyassetmeta` sidecar is approved |

Names marked planned remain provisional until a real runtime consumer defines
the data it needs. Source, idTech, CryEngine, and other engines are references
for responsibility boundaries and production lessons, not field-by-field or
binary-layout templates.

Candidate families do not authorize placeholder implementations. Each must pass
the manual's format-admission checklist, including a real producer and consumer,
versioning, bounded validation, deterministic identity, diagnostics, migration,
tests, tooling, and ownership.
