<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/README.md
//  Purpose: Documents the CypherCommon Formats folder.
//  Details: Formats contains shared magic values, binary headers, chunk table
//           declarations, and version constants for Cypher file formats.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Formats

`Formats` owns shared source-format contracts and explicitly serialized cooked
resource declarations. It does not own importers, cookers, renderer backends, or
editor widgets.

Implemented here:

- `CypherCommon_RenderFormats`: optional public umbrella for consumers that need
  the complete render-asset contract; implementation files should prefer narrow
  includes
- `CypherCommon_RenderAssetSchema`: versioned CYKV schemas for `.cyshader`,
  `.cytex`, and `.cymat` source documents
- `CypherCommon_RenderAsset`: bounded, typed, zero-copy source decoders
- `CypherCommon_CookedResource`: the versioned little-endian header and ordered
  chunk table shared by `_c` runtime resources
- `CypherCommon_CookedShader`: deterministic `CYSH` stage packaging, logical
  interface reflection, stable binding IDs, and strict borrowed views
- `CypherCommon_CookedTexture`: target-ready `CYTX` storage metadata, explicit
  alpha/residency policy, and independently validated subresource chunks
- `CypherCommon_CookedMaterial`: deterministic `CYMT` shader-interface identity,
  resolved state, texture/UV bindings, packed typed constants, and lookup views

The corresponding compiler implementations live under `src/CypherTools` and
link `Cypher::RenderFormats`. Runtime ownership and native renderer objects live
in engine modules. This folder therefore remains a reusable data/API boundary,
not an asset cooker or renderer implementation.

The remaining map, scene, mesh, animation, sound, font, navigation, flow, UI,
and package contracts are planned. Add each one with its first cooker and runtime
consumer instead of freezing speculative binary layouts.
