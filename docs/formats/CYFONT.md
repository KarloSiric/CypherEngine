<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYFONT.md
//  Purpose: Specifies font definitions (`.cyfont`, cypher.font V2): one font
//           family for the editor and the game - faces, rendering, glyph
//           coverage, metrics, OpenType features, and fallbacks - and its
//           cooked form (`.cyfont_c`).
//  Details: Identity in ADR 0009. Decoded by CypherCommon Tier2 so the
//           editor, the runtime UI, and the font compiler read one contract.
//           Source and tests are authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V2 with rendering, glyphs, metrics, features, and language fallbacks)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Font Definitions (`.cyfont`, V2)

A `.cyfont` is everything needed to draw text in one family, in the editor
and in the game: which font files make up the family, how glyphs are
rasterised (signed-distance fields for crisp scalable HUD text, bitmaps for
pixel fonts), which characters must be baked for each language, metric
corrections, OpenType features such as tabular numbers for timers, and what
to fall back to for characters the family lacks. The editor reads the source
directly; the font compiler cooks it into `.cyfont_c` atlases for the game.

## 1. Document

```cykv
@cykv 1
@schema "cypher.font" 2
{
    name = "Rajdhani"
    description = "REAP HUD and menu font."
    license = "OFL-1.1"
    monospace = false

    faces = [
        { file = "fonts/rajdhani/rajdhani-regular.ttf" weight = 400 },
        { file = "fonts/rajdhani/rajdhani-semibold.ttf" weight = 600 },
        { file = "fonts/rajdhani/rajdhani-bold.ttf" weight = 700 },
        { file = "fonts/inter/inter-variable.ttf" weight = [ 100, 900 ] italic = false ranges = [ "U+0100-017F" ] }
    ]

    rendering = {
        mode = "msdf"
        distance_range = 4.0
        atlas = { size = 2048 padding = 2 max_pages = 4 }
        sizes = [ 16, 24, 32 ]
        hinting = "light"
        antialias = true
        dynamic = true
    }

    glyphs = {
        charsets = [ "ascii", "latin1", "latin_ext_a" ]
        ranges = [ "U+2000-206F", "U+20AC" ]
        characters = "…•→←↑↓×÷°"
        languages = [ "en", "de", "hr" ]
    }

    metrics = { line_height = 1.2 ascent = 0.0 descent = 0.0 baseline_offset = 0.0 letter_spacing = 0.0 tab_width = 4 }
    features = { kern = true liga = true tnum = true }

    fallbacks = [ "fonts/noto/noto_sans.cyfont", "system:Helvetica" ]
    language_fallbacks = {
        ja = [ "fonts/noto/noto_sans_jp.cyfont" ]
        ko = [ "fonts/noto/noto_sans_kr.cyfont" ]
    }
    usage = [ "editor", "game" ]
}
```

## 2. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `name` | string | yes | family name, 1-64 bytes; what themes and UI styles refer to |
| `description`, `license` | string | no | at most 1024 / 64 bytes (an SPDX ID for `license`) |
| `monospace` | bool | no | default false; code views and consoles prefer monospace families |
| `faces` | array | yes | 1-32 faces (below) |
| `rendering` | object | no | how glyphs are rasterised (below) |
| `glyphs` | object | no | which characters are baked (below) |
| `metrics` | object | no | corrections applied on top of the face metrics (below) |
| `features` | object | no | OpenType feature tag to bool, e.g. `kern`, `liga`, `tnum`, `smcp`, `ss01` |
| `fallbacks` | array | no | up to 8: a `.cyfont` virtual path or `system:<family>` (editor only) |
| `language_fallbacks` | object | no | language tag to a fallback list tried before `fallbacks` for that language |
| `usage` | array | no | `editor`, `game`; default both. The cook skips editor-only families |

**Faces:** `file` (canonical virtual `.ttf` / `.otf` / `.ttc` path), `index`
(face index in a collection, default 0), `weight` (1-1000, default 400, or
`[min, max]` for a variable font's weight axis), `italic` (bool), `stretch`
(`condensed`, `normal`, `expanded`), `ranges` (restrict this face to code
point ranges, so one family can combine a Latin face with a symbol face).
Text asks for weight and italic; the closest face wins.

**Rendering:**

| Member | Values | Default | Meaning |
| --- | --- | --- | --- |
| `mode` | `sdf`, `msdf`, `bitmap`, `vector` | `msdf` | `msdf` keeps corners sharp at any size; `bitmap` bakes each size for pixel fonts; `vector` is editor-only direct rendering |
| `distance_range` | 1-32 | 4.0 | distance field spread in atlas pixels |
| `atlas.size` | 256-8192, power of two | 2048 | atlas page edge |
| `atlas.padding` | 0-16 | 2 | pixels between glyphs |
| `atlas.max_pages` | 1-64 | 4 | pages before the cook fails |
| `sizes` | pixel sizes | `[ 32 ]` | bake sizes; `bitmap` bakes each, distance modes use the largest |
| `hinting` | `none`, `light`, `full` | `light` | outline hinting at bake time |
| `antialias` | bool | true | `false` gives hard-edged pixel fonts |
| `dynamic` | bool | true | glyphs missing from the atlas are rasterised at runtime (text typed by players, names) |

**Glyphs:** `charsets` (`ascii`, `latin1`, `latin_ext_a`, `latin_ext_b`,
`greek`, `cyrillic`, `vietnamese`, `symbols`), `ranges` (`"U+XXXX"` or
`"U+XXXX-YYYY"`), `characters` (literal UTF-8), and `languages` (tags whose
characters the localization catalogues contain; the cook adds every
character those catalogues use). The union is baked; `dynamic` covers the
rest.

**Metrics** (in em, except `tab_width` in spaces): `line_height` (multiple of
the font size; 0 keeps the face's), `ascent`, `descent`, `baseline_offset`,
`letter_spacing` (added between glyphs), `tab_width`.

## 3. Validation

Unusable faces, fallbacks, ranges, and features are skipped and reported;
the definition fails only without a name or a usable face. A rendering or
metrics value out of range keeps its default and is reported.

## 4. Cooked form (`.cyfont_c`)

The font compiler bakes atlases per `rendering`, stores glyph metrics,
kerning pairs, and enabled features in a CYRS resource ([Renderer Asset
Contracts](RENDER_ASSETS.md)), and records the source hash. Editor-only
families (`usage = [ "editor" ]`) are not cooked.

## 5. Limits

32 faces, 8 fallbacks, 16 language fallback lists, 256 ranges, 64 KiB of
`characters`, 16 MiB per file.

## 6. Versions

V1 (2026-09-25) had `name`, `faces` (`file`, `weight`, `italic`),
`fallbacks`, and `monospace`. V2 adds the members above; it reads V1
unchanged and saving writes V2.
