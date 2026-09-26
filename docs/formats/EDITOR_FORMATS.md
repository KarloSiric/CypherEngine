<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/EDITOR_FORMATS.md
//  Purpose: Specifies the settings-family CYKV documents used by the editor
//           and shared with the runtime: settings, projects, workspaces,
//           themes, editor keymaps, dock layouts, and font families.
//  Details: Identities and scopes are decided in ADR 0009; this document is
//           the field-level contract. Source and tests are authoritative if
//           they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor And Settings Formats

| File | Schema | Code | Tests |
| --- | --- | --- | --- |
| `.cysettings` | `cypher.settings` V2 (reads V1) | `CypherCommon_Settings`, `CypherCommon_SettingsDocument` | `tests/CypherCommon/Tier2/*Settings*` |
| `.cyproject` | `cypher.project` V2 (reads V1) | `CypherCommon_ProjectManifest` | `tests/CypherCommon/Tier2/*ProjectManifest*` |
| `.cyfont` | `cypher.font` V1 | `CypherCommon_FontDefinition` | `tests/CypherCommon/Tier2/*FontDefinition*` |
| `.cyworkspace` | `cypher.workspace` V1 | `CypherEditor_Workspace` | `tests/CypherEditor/Core/Project` |
| `.cytheme` | `cypher.theme` V1 | `CypherEditor_Theme` | `tests/CypherEditor/Core/Themes` |
| `.cykeymap` | `cypher.editor_keymap` V1 | `CypherEditor_Keymap` | `tests/CypherEditor/Core/Commands` |
| `.cylayout` | `cypher.layout` V1 | `CypherEditor_Layout` | `tests/CypherEditor/Core/Layouts` |

All files are CYKV language 1. In CYKV, array elements are separated by
commas; object members are not.

## Shared Rules

These implement the persistence guarantees of
[ADR 0009](../adr/0009-editor-and-map-file-identities.md).

**The document is kept whole.** Editing goes through a
`settings_document_t` store that keeps the parsed tree, so members the
current build does not know - a newer build's settings, a plugin's section,
the reserved workspace `sharing`/`automation` members - are written back
unchanged. Saving writes the store's current schema version; loading an older
version and saving is the upgrade.

**Values are checked one at a time.** A `setting_descriptor_t` gives a known
value its type (bool, integer, real, string, enum, colour), limits, default,
label, and settings page. A read reports `VALUE`, `ABSENT`, or `INVALID` with a
problem code; an invalid value never fails the file. Reals accept integers.
Colours are `"#rrggbb"` or `"#rrggbbaa"`, written in lower case, opaque colours
without alpha.

**Scopes resolve most specific first:** workspace, project, user, then the
descriptor default (`Setting_Resolve`, `EditorScopes_Make`). Writes into a scope
go through `Setting_WriteOverride`, which removes the scope's own entry when
the value equals the inherited one, so files stay sparse and improved defaults
reach users.

**Files are saved safely** (`EditorSettingsFile_*`):

| Situation | Behaviour |
| --- | --- |
| Save | write `<file>.tmp`, copy the current file to `<file>.bak`, atomically replace `<file>` |
| Load, file missing | empty scope; saving creates the file |
| Load, file damaged, `.bak` readable | damaged file copied to `<file>.broken`, backup loaded, saving allowed |
| Load, file and backup unusable | scope runs on inherited values; saving blocked (`SAVE_BLOCKED`) until the user accepts an overwrite, which first copies the damaged file to `<file>.broken` |

A whole document is rejected only for text that is not CYKV, a wrong
`@cykv`/`@schema` identity, or an unsupported version.

## `.cysettings` - `cypher.settings` V2

An open object of sections. Any section or member is allowed; owners read
theirs through descriptors.

```cykv
@cykv 1
@schema "cypher.settings" 2
{
    display = { width = 1920 height = 1080 mode = "borderless" vsync = true }
    editor = { viewport = { grid_size = 16 } }
}
```

Engine display section (`CypherSettings_DisplayDescriptors`):

| Path | Type | Default | Limits |
| --- | --- | --- | --- |
| `display.width` | integer | 1280 | 320-16384 |
| `display.height` | integer | 720 | 200-16384 |
| `display.mode` | enum | `windowed` | `windowed`, `borderless`, `fullscreen` |
| `display.vsync` | bool | true | - |

`CypherSettings_Decode` reads V1 and V2. An invalid display value keeps its
default and is reported as a WARNING diagnostic (`/display/width` style path);
only a wrong header fails. V1 files are valid V2 content.

## `.cyproject` - `cypher.project` V2

```cykv
@cykv 1
@schema "cypher.project" 2
{
    id = "reap"
    name = "REAP"
    game = "reap"
    start_map = "maps/facility.cymap"
    maps_path = "maps"
    search_paths = [ "game", "engine" ]
    settings = { editor = { grid = 8 } }
    map_defaults = { gravity = 800.0 }
}
```

| Member | Type | Required | Constraint |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier, 1-64 bytes |
| `name` | string | yes | 1-128 bytes |
| `start_map` | string | no (V1: yes) | canonical virtual path ending `.cymap` |
| `search_paths` | array of strings | no | 1-64 unique canonical virtual paths, highest priority first |
| `game` | string | no | stable identifier of the game profile, 1-64 bytes |
| `maps_path` | string | no | canonical virtual path; default `maps` |
| `settings` | object | no | team settings scope, read with descriptors |
| `map_defaults` | object | no | default settings for new maps |

The root is open. Identity, start map, and content roots are validated
strictly (`INVALID_PROJECT_ID`, `INVALID_START_MAP`, `INVALID_SEARCH_PATH`,
`DUPLICATE_SEARCH_PATH`, `INVALID_GAME`, `INVALID_MAPS_PATH`,
`UNSUPPORTED_VERSION`) because a wrong mount set loads the wrong game data.
Game-specific settings live in sections named by the game profile.

## `.cyworkspace` - `cypher.workspace` V1

```cykv
@cykv 1
@schema "cypher.workspace" 1
{
    id = "7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77"
    name = "main"
    owner = "Karlo"
    project = "../reap/reap.cyproject"
    content = "wip"
    settings = { editor = { autosave_minutes = 5 } }
    state = { open_maps = [ "maps/facility.cymap" ] layout = "four_view" }
}
```

| Member | Type | Required | Constraint |
| --- | --- | --- | --- |
| `id` | string | yes | nonzero UUID; new workspaces get a random one |
| `name` | string | yes | 1-128 bytes |
| `project` | string | yes | native path to the `.cyproject`, relative to the workspace file, 1-1024 bytes |
| `owner` | string | no | display name, at most 128 bytes |
| `content` | string | no | relative path inside the workspace, no `..`, no root or drive; overlays project content |
| `settings` | object | no | personal settings scope |
| `state` | object | no | remembered state: open maps, layout, bookmarks |
| `sharing`, `automation` | - | reserved | peer sharing and MCP policy (planned); preserved untouched |

Identity failures reject the workspace; bad optional members are dropped and
reported through `workspace_problem_flags_t`.

## `.cytheme` - `cypher.theme` V1

```cykv
@cykv 1
@schema "cypher.theme" 1
{
    id = "midnight"
    name = "Midnight"
    base = "charcoal"
    colors = { "ui.background" = "#101418" "viewport.grid.minor" = "#1c2a36" }
    fonts = { console = { family = "JetBrains Mono" size = 10.0 weight = 400 } }
    metrics = { "ui.icon_size" = 20 }
}
```

| Member | Type | Constraint |
| --- | --- | --- |
| `id` | string | stable identifier; how other themes name this one as `base` |
| `name` | string | display name, at most 128 bytes |
| `base` | string | theme ID this theme builds on; chains stop at 8 levels, a missing base, or a cycle |
| `colors` | object | token ID to colour |
| `fonts` | object | token ID to `{ family, size, weight }`; each field optional, size 1-200 points, weight 1-1000 |
| `metrics` | object | token ID to number within the token's registered range |

Token IDs are dotted lower-case identifiers (`viewport.grid.minor`) stored
verbatim as member names. Modules register the tokens they draw with
(`theme_token_t`, `EditorThemeRegistry_Register`); a token resolves through the
theme's base chain, font fields independently, then to its registered default.
`EditorTheme_Audit` lists unknown tokens (kept, perhaps from an unloaded
plugin), tokens in the wrong section, and invalid values for the theme editor.

## `.cykeymap` - `cypher.editor_keymap` V1

Editor shortcuts only. Game input is `.cyinput` / `.cybindings`
([Input Actions](INPUT_ACTIONS.md)); the schema IDs keep the families apart.

```cykv
@cykv 1
@schema "cypher.editor_keymap" 1
{
    id = "karlo"
    name = "Karlo's keys"
    base = "cypher_default"
    bindings = {
        global = { "file.save" = [ "Ctrl+S" ] "view.console" = [ "Backquote" ] }
        "map.viewport" = { "map.tool.clip" = [ "Shift+X" ] "map.tool.vertex" = [] }
    }
}
```

`bindings` maps an editor context (`global`, `map.viewport`, ...) to command IDs
(`module.command`, lower-case stable identifiers) and each command to up to
four chords. An empty list explicitly unbinds a command the base binds; an
entry whose chords are all unparseable is invalid and the base applies.

Chord text: modifiers `Ctrl`, `Alt`, `Shift`, `Meta` (aliases Control, Option,
Cmd, Command, Super, Win; any case), then one key: `A`-`Z`, `0`-`9`, `F1`-`F24`,
`Num0`-`Num9`, `NumAdd`, `NumSubtract`, `NumMultiply`, `NumDivide`,
`NumDecimal`, `NumEnter`, `Space`, `Escape`, `Tab`, `Backspace`, `Enter`,
`Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, arrow names, and
punctuation words `Backquote`, `Minus`, `Equal`, `BracketLeft`, `BracketRight`,
`Backslash`, `Semicolon`, `Quote`, `Comma`, `Period`, `Slash` (the symbols are
also accepted). Sequences of up to four strokes are joined by `", "`. The
canonical form orders modifiers Ctrl, Alt, Shift, Meta and uses the first key
name above. Reverse lookup and conflict detection honour overrides.

## `.cylayout` - `cypher.layout` V1

```cykv
@cykv 1
@schema "cypher.layout" 1
{
    id = "four_view"
    name = "Four views"
    workspace = "map"
    root = {
        split = "horizontal"
        sizes = [ 0.2, 0.6, 0.2 ]
        children = [
            { tabs = [ "map.tools" ] },
            { split = "vertical" sizes = [ 0.5, 0.5 ] children = [
                { tabs = [ "map.viewport.3d" ] },
                { tabs = [ "map.viewport.top" ] }
            ] },
            { tabs = [ "map.outliner", "map.properties" ] current = 1 }
        ]
    }
    floating = [ { x = 80 y = 80 width = 640 height = 320 root = { tabs = [ "console" ] } } ]
}
```

A node is either a split (`split` = `horizontal`/`vertical`, `children`,
optional `sizes`) or a tab group (`tabs`, optional `current`). Limits: 256
nodes, depth 16, 64 children or tabs per node, 32 floating windows. Sizes are
relative weights normalised per split; missing or mismatched sizes become equal
shares. Malformed tab entries are dropped; unregistered panel IDs are kept for
the GUI to skip. Structural damage rejects the layout so the editor falls back
to its default layout, and a failed decode keeps the previous layout.

## `.cyfont` - `cypher.font` V1

```cykv
@cykv 1
@schema "cypher.font" 1
{
    name = "Inter"
    faces = [
        { file = "fonts/inter/inter-regular.ttf" weight = 400 },
        { file = "fonts/inter/inter-italic.ttf" weight = 400 italic = true }
    ]
    fallbacks = [ "fonts/noto/noto_sans.cyfont", "system:Helvetica" ]
    monospace = false
}
```

| Member | Type | Constraint |
| --- | --- | --- |
| `name` | string | required, 1-64 bytes |
| `faces` | array | 1-32 faces; `file` a canonical virtual `.ttf`/`.otf` path, `weight` 1-1000 (default 400), `italic` bool |
| `fallbacks` | array | up to 8: a `.cyfont` virtual path or `system:<family>` |
| `monospace` | bool | default false |

Unusable faces and fallbacks are skipped and reported; the definition fails
only without a name or a usable face. Atlas and glyph-range fields for the
runtime font compiler come in a later version.
