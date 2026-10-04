<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYTHEME.md
//  Purpose: Specifies editor themes (`.cytheme`, cypher.theme V2): the
//           complete set of colours, fonts, metrics, and choices every
//           Cypher editor draws with, and how themes are saved and resolved.
//  Details: Identity in ADR 0009, framework rules in ADR 0008. The token
//           catalogue here is the contract between the theme editor, the
//           stylesheet generator, and every viewport; source and tests are
//           authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V2 with the complete token catalogue)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor Themes (`.cytheme`, V2)

A theme is everything about how the editor looks: interface chrome, every
viewport colour (backgrounds, grid lines, selection, wireframes, entities,
gizmos, tool previews), console colours, fonts, line widths, and sizes. Users
build themes in Mason's theme editor, which saves the whole theme; the file is
then plain text anyone can edit, share, or commit to a project.

## 1. Files and scopes

| Scope | Location | Notes |
| --- | --- | --- |
| Built-in | editor resources: `charcoal.cytheme` (the default) and `presets/*.cytheme` | read-only; see 1.1 |
| User | `<config>/Mason/themes/<id>.cytheme` | themes the user made or imported |
| Project | `<project>/editor/themes/<id>.cytheme` | team themes, committed |

The active theme is the setting `editor.ui.theme` (a theme ID), so a project
or workspace can pick one. IDs are unique across scopes; a user theme shadows
a project theme of the same ID, which shadows a built-in one.

### 1.1 Bundled presets

Eleven presets ship beside Charcoal, ported from the TileEditor's colour
presets: `radiant_dark` (the TileEditor's own look), `slate`,
`hammer_charcoal`, `radiant_light`, `midnight`, `warm_workshop`,
`blueprint_blue`, `graphite`, `high_contrast_dark`, `coastal_dusk`, and
`sandstone_light`. Each is a partial theme that names `charcoal` as its
`base` and sets only base colours (interface, 2D and 3D backgrounds, grid,
axes, geometry, selection), so every derived shade follows them. The
framework loads them right after the built-in theme
(`EDITOR_PRESET_THEME_FOLDER`); user theme folders load later, so a user
theme with a preset's ID takes its place.

Settings > Appearance lists the default theme, then the presets, then user
themes. Choosing one applies it and writes `editor.ui.theme`; editing
colours or sizes there previews a modified copy until it is saved as a user
theme (section 3) or reverted. Only user themes can be deleted; the file goes
to the trash.

## 2. Document

```cykv
@cykv 1
@schema "cypher.theme" 2
{
    id = "midnight"
    name = "Midnight"
    author = "Karlo"
    description = "Blue-black chrome, bright grid for night sessions."
    appearance = "dark"
    colors = {
        "ui.background" = "#101418"
        "ui.panel" = "#0c1014"
        "ui.text" = "#d6dde6"
        "ui.accent" = "#4aa3ff"
        "ui.border" = "auto"
        "viewport.background.2d" = "#0b1520"
        "viewport.grid.minor" = "#132233"
        "viewport.grid.major" = "#1f3a57"
        "viewport.selection" = "#ffb347"
    }
    fonts = {
        ui = { family = "system" size = 11.0 weight = 400 }
        console = { family = "JetBrains Mono" size = 10.0 weight = 400 }
    }
    metrics = {
        "viewport.grid.line_width" = 1.0
        "viewport.selection.line_width" = 2.5
    }
    choices = {
        "viewport.grid.style" = "lines"
        "ui.density" = "compact"
    }
}
```

The example is shortened; a theme saved by Mason lists every registered token
(section 4), in catalogue order.

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier, 1-64 bytes; how settings and other themes name it |
| `name` | string | yes | display name, 1-128 bytes |
| `author` | string | no | at most 128 bytes |
| `description` | string | no | at most 1024 bytes |
| `appearance` | `"dark"` / `"light"` | no | tells the platform (title bars, native dialogs) which way to lean; default `dark` |
| `base` | string | no | theme ID this theme builds on; tokens it does not set come from the base. Chains stop at 8 levels, a missing base, or a cycle |
| `colors` | object | no | token ID to colour: `"#rrggbb"`, `"#rrggbbaa"`, or `"auto"` for derived tokens |
| `fonts` | object | no | token ID to `{ family, size, weight }`; each field optional; size 1-200 points; weight 1-1000; family a system family, a `.cyfont` virtual path, or `system` / `system-mono` |
| `metrics` | object | no | token ID to a number within the token's range |
| `choices` | object | no | token ID to one of the token's listed values |

Token IDs are dotted lower-case identifiers stored verbatim as member names,
never split into nested objects.

## 3. Saving and resolving

**Mason saves complete themes.** Saving from the theme editor writes the
header and then every registered token in catalogue order, grouped by
section. Derived tokens the user has not changed are written as `"auto"`, so
the file shows every token while still following the base colours: change
`ui.background` in the file and every `auto` border, hover, and gutter shade
follows. Tokens from a plugin that is not loaded are kept where they are
(`EditorTheme_KeepUnknownTokens`).

A theme saved from the editor names **no `base`**. It already lists every
token, and in a root theme `"auto"` always means the formula; with a base,
`"auto"` would pass to the base instead (see resolution below), so a derived
colour the base pins could never be returned to its formula. Saving goes to
`<config>/Mason/themes/<id>.cytheme` (atomically), and the built-in theme's
ID cannot be saved over. The saved theme becomes active and is recorded in
`editor.ui.theme`.

Values chosen in the editor are always written, even when they equal what
the formula gives at that moment: a pinned colour stays pinned when its
inputs change later. "Follow Formula" writes `"auto"`.

**Hand-written themes may be partial.** A theme can set a few tokens and name
a `base`; everything else resolves from the base chain.

**Resolution, per token:** the first theme in the chain (the theme, then its
bases) with a valid value wins. `"auto"` and absent values pass to the next
theme. When no theme sets the token, a derived token is computed from its
formula and any other token uses its registered default. An invalid value is
skipped, counted, and flagged by the theme editor; it never fails the theme.
Font fields resolve independently, so a theme can change only the console
font size.

**Audit** (`EditorTheme_Audit`) lists unknown tokens (kept; perhaps from an
unloaded plugin), values in the wrong section, and invalid values.

## 4. Token catalogue

Modules register the tokens they draw with (`theme_token_t`); the framework
tokens below are registered by EditorGui, workspace tokens by each workspace.
**D** marks derived colours: their default is the formula, shown with `mix(a,
b, t)` (linear blend from `a` to `b`), `lighter(c, %)`, and `darker(c, %)`.

### 4.1 Interface: base colours

| Token | Default | Used for |
| --- | --- | --- |
| `ui.background` | `#303032` | window, dialogs, menus |
| `ui.panel` | `#27282b` | lists, trees, text areas |
| `ui.text` | `#dce0e5` | all text |
| `ui.accent` | `#e1a03e` | active tool, focus, current tab, selection accents |

### 4.2 Interface: chrome (derived)

| Token | D formula | Used for |
| --- | --- | --- |
| `ui.chrome` | `mix(background, text, 0.012)` | menu bar, toolbars, tool strip |
| `ui.header` | `mix(background, text, 0.075)` | dock title bars |
| `ui.border` | `mix(background, text, 0.20)` | control borders, scrollbar handles |
| `ui.border.highlight` | `lighter(border, 110)` | hovered borders, dock title top line |
| `ui.edge` | `mix(panel, black, 0.32)` | separators, splitters, frame edges |
| `ui.inset` | `mix(panel, black, 0.16)` | text inputs, selected rows, tooltips |
| `ui.deepest` | `darker(edge, 115)` | console, previews, view number badges |
| `ui.button` | `mix(background, text, 0.09)` | buttons, header sections, status bar |
| `ui.hover` | `mix(button, #98bdda, 0.30)` | restrained light blue on hovered controls and menu items |
| `ui.pressed` | `inset` | pressed buttons |
| `ui.checked` | `mix(background, text, 0.06)` | neutral checked tool fill; border uses accent |
| `ui.checked.hover` | `mix(checked, #98bdda, 0.22)` | blue hover surface on checked tools; orange rim remains visible |
| `ui.accent.light` | `lighter(accent, 115)` | checked tool text |
| `ui.text.muted` | `mix(background, text, 0.68)` | secondary text, placeholders |
| `ui.text.disabled` | `mix(background, text, 0.42)` | disabled text and icons |
| `ui.text.selected` | `mix(text, accent, 0.34)` | text in selected rows and tabs |
| `ui.selection` | `mix(panel, accent, 0.12)` | selected text and row background |
| `ui.row.alternate` | `mix(panel, text, 0.035)` | alternating list rows |
| `ui.focus` | `accent` | focused input border |
| `ui.link` | `status.info.text` | links in help and diagnostics |

### 4.3 Interface: status colours (derived)

Status hues keep their meaning in every theme; the formulas tune lightness to
the background (`light` below is 0.68 on dark themes, 0.36 on light ones).

| Token | D formula |
| --- | --- |
| `ui.status.error.text` | `mix(text, hsl(0.010, sat, light), 0.58)` |
| `ui.status.error.background` | `mix(panel, hsl(0.010, sat, light), 0.13)` |
| `ui.status.error.border` | `mix(border, hsl(0.010, sat, light), 0.52)` |
| `ui.status.warning.text` | `accent.light` |
| `ui.status.warning.background` | `mix(panel, accent, 0.13)` |
| `ui.status.warning.border` | `accent` |
| `ui.status.success.text` | `mix(text, hsl(0.365, sat, light), 0.48)` |
| `ui.status.success.border` | `mix(border, hsl(0.365, sat, light), 0.48)` |
| `ui.status.info.text` | `mix(text, hsl(0.565, sat, light), 0.48)` |
| `ui.status.info.border` | `mix(border, hsl(0.565, sat, light), 0.48)` |

`sat` is the accent's saturation clamped to 0.58-0.90 (0.70 for a grey
accent).

### 4.4 Viewport: frame and overlays

| Token | Default | Used for |
| --- | --- | --- |
| `viewport.gutter` | D `mix(background, black, 0.48)` | space between viewport panes |
| `viewport.gutter.light` | D `mix(gutter, text, 0.13)` | gutter bevel highlight |
| `viewport.gutter.shadow` | D `mix(gutter, black, 0.55)` | gutter bevel shadow |
| `viewport.gutter.hover` | D `mix(gutter, text, 0.08)` | hovered pane splitter |
| `viewport.header` | D `mix(panel, background.2d, 0.24)` | pane title strip |
| `viewport.header.active` | D `mix(button, background.2d, 0.15)` | active pane title strip |
| `viewport.active_border` | `#d93e36` | active pane outline (when enabled) |
| `viewport.overlay.background` | D `mix(background.3d, text, 0.055)` | on-view labels and readouts |
| `viewport.overlay.text` | D `text.muted` | on-view text |
| `viewport.overlay.border` | D `border` | on-view label frames |

### 4.5 Viewport: backgrounds and grid

| Token | Default | Used for |
| --- | --- | --- |
| `viewport.background.2d` | `#000000` | top, front, and side views |
| `viewport.background.3d` | `#000000` | 3D view (top of the gradient) |
| `viewport.background.3d.bottom` | D `background.3d` | 3D view gradient bottom; equal colours mean flat |
| `viewport.grid.minor` | `#1f1f1f` | every grid line |
| `viewport.grid.major` | `#3a3a3a` | every `editor.grid.major_every`-th line |
| `viewport.grid.band` | D `grid.major` | legacy compatibility token; colored grid bands are no longer drawn |
| `viewport.grid.highlight` | D `mix(grid.major, text, 0.15)` | lines every `editor.grid.highlight_every` units |
| `viewport.grid.origin` | D `mix(grid.major, text, 0.35)` | the lines through 0 |
| `viewport.grid.3d` | D `mix(background.3d, grid.major, 0.6)` | 3D floor grid |
| `viewport.axis.x` | `#de524c` | X axis everywhere (keep red) |
| `viewport.axis.y` | `#52be69` | Y axis everywhere (keep green) |
| `viewport.axis.z` | `#4b8be8` | Z axis everywhere (keep blue) |

### 4.6 Viewport: objects

| Token | Default | Used for |
| --- | --- | --- |
| `viewport.wire.world` | `#c8ccd2` | world brush edges in 2D views |
| `viewport.wire.entity` | `#6faacd` | brush-entity edges (triggers, doors) without a class colour |
| `viewport.wire.mesh` | `#9fb5c7` | mesh edges |
| `viewport.wire.patch` | `#86c4a7` | patch control cages |
| `viewport.wire.terrain` | `#b6a06f` | heightfield edges |
| `viewport.face.world` | `#8a8f96` | untextured faces in flat shading |
| `viewport.selection` | `#d0ba62` | restrained yellow selected edges in 2D, selection outline in 3D |
| `viewport.selection.fill` | `#d0ba6240` | selected face tint in 3D |
| `viewport.selection.bounds` | D `selection` | dashed selection box and its handles |
| `viewport.hover` | `#8cba87` | restrained green on the object under the cursor before a click |
| `viewport.entity.default` | `#d946ef` | point entities whose class has no colour |
| `viewport.entity.unknown` | `#ff00ff` | entities whose class is missing from the `.cygame` |
| `viewport.entity.helper` | `#e6d36a` | radius spheres, cones, direction arrows |
| `viewport.entity.name` | D `text` | entity name labels |
| `viewport.io.line` | `#4fd1c5` | output connections of the selection |
| `viewport.io.broken` | D `status.error.text` | outputs whose target name matches nothing |
| `viewport.path` | `#f6ad55` | path links between path nodes |
| `viewport.light.radius` | `#fff3b0` | light falloff radius |
| `viewport.model.bounds` | `#7f9cf5` | model bounding boxes |
| `viewport.decal` | `#f687b3` | decal and overlay outlines |
| `viewport.invalid` | `#ff3b30` | invalid geometry, leaks, errors in view |
| `viewport.hidden` | D `mix(background.2d, text, 0.25)` | ghosted objects of hidden layers when shown dimmed |

### 4.7 Viewport: tools and gizmos

| Token | Default | Used for |
| --- | --- | --- |
| `viewport.tool.preview` | `#ffffff` | block, entity, and shape creation previews |
| `viewport.tool.preview.fill` | `#ffffff20` | creation preview fill in 3D |
| `viewport.transform.source` | `#ed756f` | dashed starting-position outline and travel guide during a nonzero move; distinct from hidden-object and clip ghosts |
| `viewport.clip.plane` | `#ff6b6b` | clip plane line and points |
| `viewport.clip.kept` | `#68d391` | part kept by the clip |
| `viewport.clip.removed` | `#fc8181` | part removed by the clip |
| `viewport.vertex` | `#e2e8f0` | vertex handles |
| `viewport.vertex.selected` | D `selection` | selected vertices |
| `viewport.vertex.hover` | D `hover` | vertex under the cursor |
| `viewport.edge.selected` | D `selection` | selected edges (vertex and edge tools) |
| `viewport.face.selected` | `#ff4d4d60` | faces selected by the texture tool |
| `viewport.handle` | D `selection` | resize and rotate handles |
| `viewport.handle.hover` | D `lighter(selection, 130)` | hovered handle |
| `viewport.gizmo.x` | D `axis.x` | gizmo X |
| `viewport.gizmo.y` | D `axis.y` | gizmo Y |
| `viewport.gizmo.z` | D `axis.z` | gizmo Z |
| `viewport.gizmo.view` | `#d0d0d0` | screen-space ring and free-move centre |
| `viewport.gizmo.hover` | `#ffe066` | hovered gizmo part |
| `viewport.gizmo.active` | D `accent` | dragged gizmo part |
| `viewport.measure` | `#90cdf4` | measuring tool lines and readouts |
| `viewport.cordon` | `#ffcc00` | cordon bounds |
| `viewport.cordon.outside` | `#00000080` | dimming outside an active cordon |
| `viewport.camera` | `#a0aec0` | camera icons and frustums in 2D |
| `viewport.camera.active` | D `accent` | the 3D view's camera in 2D |

### 4.8 Console

| Token | Default | Used for |
| --- | --- | --- |
| `console.background` | D `deepest` | output area |
| `console.text` | D `text` | info records |
| `console.muted` | D `mix(background, text, 0.62)` | trace and debug records |
| `console.warning` | D `accent` | warnings |
| `console.error` | D `status.error.text` | errors and fatals |
| `console.success` | D `status.success.text` | completed builds, passed checks |
| `console.command` | D `text.selected` | echoed command lines |
| `console.prompt` | D `accent` | the `>` prompt |

### 4.8b Code

The code editor (the Database View's shader and recipe source). Keys in
CYKV recipes use `code.type`.

| Token | Default | Used for |
| --- | --- | --- |
| `code.background` | D `deepest` | editor background |
| `code.text` | D `text` | plain source |
| `code.current_line` | D `mix(code.background, text, 0.06)` | the cursor's line |
| `code.line_number` | D `text.disabled` | gutter numbers |
| `code.keyword` | `#cc8a4e` | `if`, `return`, `uniform`, `layout`; CYKV `true` / `false` / `null` |
| `code.type` | `#7fa9c4` | `vec3`, `sampler2D`; recipe keys |
| `code.builtin` | `#b39dcf` | `texture`, `mix`, `gl_Position` |
| `code.number` | `#79a6c9` | numbers |
| `code.string` | `#93b077` | strings and asset paths |
| `code.comment` | `#7d8389` | comments (italic) |
| `code.preprocessor` | `#c2b260` | `#version`, `#define`, `@schema` |

### 4.9 Fonts

| Token | Default | Used for |
| --- | --- | --- |
| `ui` | `system`, 11 pt, 400 | interface text |
| `ui.heading` | `system`, 11 pt, 600 | dock titles, group boxes |
| `ui.small` | `system`, 10 pt, 400 | status bar, pane headers, badges |
| `console` | `system-mono`, 11 pt, 400 | console and log views |
| `code` | `system-mono`, 12 pt, 400 | script and text editors |
| `viewport.labels` | `system`, 9 pt, 500 | entity names, measurements, on-view readouts |

### 4.10 Metrics

| Token | Default | Range | Used for |
| --- | --- | --- | --- |
| `ui.icon_size` | 24 | 12-64 | toolbar icons (logical px) |
| `ui.tool_strip.icon_size` | 32 | 12-64 | tool strip icons |
| `ui.icon.checked_color_strength` | 0.65 | 0-1 | original illustration colors on checked icons; independent of the accent border |
| `ui.icon.hover_color_strength` | 0.35 | 0-1 | original illustration color mixed into enabled, unchecked icons on hover; disabled icons stay gray |
| `ui.tool_strip.spacing` | 1 | 0-8 | spacing between palette buttons (logical px) |
| `ui.tool_strip.separator_height` | 3 | 1-12 | compact dividers between palette tool groups (logical px) |
| `ui.menu.icon_size` | 16 | 12-32 | menu icons |
| `ui.spacing` | 4 | 0-24 | space between controls |
| `ui.padding` | 4 | 0-24 | inner padding of controls |
| `ui.row_height` | 20 | 14-48 | list and tree rows |
| `ui.splitter_width` | 3 | 1-12 | dock splitters |
| `ui.scrollbar_width` | 9 | 4-24 | scrollbars |
| `ui.border_radius` | 2 | 0-12 | button and input corners |
| `viewport.splitter_width` | 6 | 1-16 | gutters between viewport panes |
| `viewport.grid.line_width` | 1.0 | 0.5-4 | minor lines |
| `viewport.grid.major.line_width` | 1.0 | 0.5-4 | major and highlight lines |
| `viewport.grid.origin.line_width` | 1.5 | 0.5-6 | origin lines |
| `viewport.grid.dot_size` | 2.0 | 1-8 | dots in dot style |
| `viewport.wire.line_width` | 1.2 | 0.5-4 | object edges |
| `viewport.selection.line_width` | 2.0 | 0.5-6 | selected edges and outline |
| `viewport.vertex.size` | 6 | 2-24 | vertex handles (px) |
| `viewport.handle.size` | 7 | 3-24 | resize handles (px) |
| `viewport.gizmo.size` | 96 | 32-256 | gizmo size on screen (px) |
| `viewport.gizmo.line_width` | 2.0 | 1-8 | gizmo strokes |
| `viewport.entity.icon_size` | 16 | 8-64 | entity sprites and icons in views |

### 4.11 Choices

A choice token takes one of a fixed list of strings.

| Token | Default | Values | Used for |
| --- | --- | --- | --- |
| `ui.density` | `normal` | `compact`, `normal`, `comfortable` | scales padding, spacing, and row height by 0.8 / 1 / 1.25 |
| `viewport.grid.style` | `lines` | `lines`, `dots` | 2D grid drawing |
| `viewport.selection.style` | `solid` | `solid`, `dashed` | selected edges in 2D |
| `viewport.background.3d.style` | `flat` | `flat`, `gradient` | 3D background |

### 4.12 Workspace tokens

Workspaces add their own groups (for example `tile.*` for the TileEditor's
floor, wall, stair, and door colours, `material.*` for the material editor's
preview backdrop). They follow the same rules and appear in the same theme
file; entity colours per class come from the `.cygame` class definitions, not
the theme.

## 5. Limits

2,048 tokens per section, 96-byte token IDs, 259-byte font families, base
chains 8 deep. A theme file is limited to 16 MiB like every settings-family
document.

## 6. Versions

V1 (2026-09-25) had the same shape without `author`, `description`,
`appearance`, `choices`, or `"auto"`. V2 reads V1 unchanged; saving writes V2.
