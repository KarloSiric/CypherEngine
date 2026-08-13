<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/FORMAT_CATALOG.md
//  Purpose: Tracks Cypher source and cooked format maturity.
//  Details: The catalog distinguishes implemented contracts from reserved names
//           so architectural plans cannot be mistaken for shipping formats.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Format Catalog

## Maturity Labels

| Label | Meaning |
| --- | --- |
| Implemented | Parser/schema/typed contract exists and is tested. |
| Active | The next vertical slice is being built against a real consumer. |
| Planned | Purpose and provisional name are recorded; layout is not frozen. |

## Foundation And Configuration

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Generic structured data | CYKV text | CYKV binary pack where useful | Implemented |
| Project manifest | `.cyproject` | none | Implemented |
| User settings | `.cysettings` | none | Implemented |
| Command/CVar script | `.cfg` / `.cycfg` | none | Implemented runtime family |
| Generic cooked resource | n/a | `CYRS` envelope | Implemented |

## Renderer Vertical Slice

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Shader recipe | `.cyshader` | `.cyshader_c` | Source/cooked contracts and deterministic cooker implemented; runtime use deferred |
| Texture recipe | `.cytex` | `.cytex_c` | Source/cooked contracts and deterministic PNG/JPEG/EXR cooker implemented; runtime use deferred |
| Material | `.cymat` | `.cymat_c` | Source/cooked contracts and dependency-validating cooker implemented; reflection/runtime use deferred |
| Mesh recipe | `.cymesh` | `.cymesh_c` | Planned |

## World And Simulation

| Purpose | Source | Cooked/runtime | Status |
| --- | --- | --- | --- |
| Map/world | `.cymap` | `.cymap_c` | Planned |
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
| Package archive | `.cypak` | Existing subsystem; future format review required |
| Resource/build manifest | `.cymanifest` | Planned |
| Derived-data cache | internal | Planned |

Names marked planned remain provisional until a real runtime consumer defines
the data it needs. Source 1/2, idTech, and other engines are references for
responsibility boundaries, not templates to reproduce field by field.
