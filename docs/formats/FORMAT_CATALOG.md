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
| Active | A frozen lower-level contract exists and the next integration layer is being implemented. |
| Planned | Purpose and provisional name are recorded; the source or binary layout is not frozen. |

A format can have different maturity at each layer. The detailed source syntax,
binary layouts, limits, and deliberate compiler gates live in
[Renderer Asset Contracts](RENDER_ASSETS.md).

## Foundation And Configuration

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Generic structured data | CYKV text V1 | CYKV binary pack where useful | Implemented |
| Project manifest | `.cyproject` | None | Implemented |
| User settings | `.cysettings` | None | Implemented |
| Command/CVar script | `.cfg` / `.cycfg` | None | Implemented runtime family |
| Generic cooked resource | N/A | `CYRS` container V1 | Implemented |

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
| Map/world | `.cymap`: Tile Editor schemas V1, V2, and V3 | `.cymap_c` | Authored source, deterministic persistence, validation, generated blockout geometry, and editor/runtime preview implemented; shared cooker and production world runtime planned |
| General scene | `.cyscene` | `.cyscene_c` | Planned |
| Prefab/entity template | `.cyprefab` | `.cyprefab_c` | Planned |
| Physics setup | `.cyphys` | `.cyphys_c` | Planned |
| Navigation | `.cynav` | `.cynav_c` | Planned |
| Mission/logic graph | `.cyflow` | `.cyflow_c` | Planned |

## Character And Presentation

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Skeleton | `.cyskel` | `.cyskel_c` | Planned |
| Animation clip | `.cyanim` | `.cyanim_c` | Planned |
| Particle system | `.cyparticle` | `.cyparticle_c` | Planned |
| Sound recipe/event | `.cysnd` | `.cysnd_c` | Planned |
| Font recipe | `.cyfont` | `.cyfont_c` | Planned |
| UI layout/style | `.cyui` | `.cyui_c` | Planned |
| Cinematic sequence | `.cycine` | `.cycine_c` | Planned |

## Distribution

| Purpose | Format | Status |
| --- | --- | --- |
| Package archive | `.cypak` | Deterministic V10 reader/writer and FileSystem mount implemented with uncompressed payloads and per-file hashes; compression, archive signatures, and a formal external specification remain future work |
| Resource/build manifest | `.cymanifest` | Planned |
| Derived-data cache | Internal | Planned |

Names marked planned remain provisional until a real runtime consumer defines
the data it needs. Source, idTech, CryEngine, and other engines are references
for responsibility boundaries and production lessons, not field-by-field or
binary-layout templates.
