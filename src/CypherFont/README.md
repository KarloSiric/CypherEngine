<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFont/README.md
//  Purpose: Defines the ownership and first implementation gate of CypherFont.
//  Details: Prevents font resources, shaping, glyph caching, UI layout, and GPU
//           execution from becoming one ambiguous text-rendering subsystem.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherFont

`CypherFont` is the planned renderer-neutral runtime text service. It is a
separate owner because UI, the developer console, debug labels, subtitles,
captions, and world-space labels all need text without owning font files or a
glyph atlas independently.

## Ownership

CypherFont will own:

- loaded font faces, families, styles, weights, and fallback chains;
- Unicode shaping inputs and shaped glyph runs;
- glyph metrics, line metrics, kerning/shaping output, and text measurement;
- strict malformed-text and missing-glyph policy;
- glyph rasterization or cooked glyph-image ingestion;
- bounded glyph caches and atlas allocation/eviction policy;
- renderer-neutral glyph instances and text draw data;
- font memory and cache diagnostics.

CypherFont will not own:

- widget layout, focus, navigation, or interaction state (`CypherUI`);
- localization catalogs and language selection;
- native GPU textures, buffers, pipelines, or draw execution (`CypherRender`);
- source-font conversion and coverage reports (`CypherFontCompiler`);
- Qt tool typography.

## First Implementation Gate

This directory is a scaffold. It becomes an implemented `Cypher::Font` target
only when one end-to-end text path exists:

1. a versioned cooked font resource and loader;
2. one font face and family record;
3. strict UTF-8 decoding and a documented replacement policy;
4. shaping/fallback suitable for the supported languages;
5. glyph and line metrics with deterministic measurement;
6. a bounded atlas or cooked page set;
7. immutable renderer-neutral text draw data;
8. one line displayed by a runtime sample;
9. tests for malformed UTF-8, missing glyphs, fallback, measurement, wrapping,
   atlas capacity, and stale resource handles.

The public layout API will be designed with the selected shaping and font
rasterization libraries. Cypher must not freeze a codepoint-only API that later
cannot represent ligatures, combining marks, bidirectional text, or fallback.

See [ADR 0006](../../docs/adr/0006-runtime-subsystem-structure.md) and the
[CryEngine 1 subsystem research](../../docs/cryengine1_subsystem_research.md).
