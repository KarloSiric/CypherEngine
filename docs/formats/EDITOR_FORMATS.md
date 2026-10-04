<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/EDITOR_FORMATS.md
//  Purpose: Indexes Mason's file formats and states the rules they share:
//           whole-document editing, per-value checking, scopes, and safe
//           saving.
//  Details: Identities and scopes are decided in ADR 0009. Each format has
//           its own field-level specification, linked from the table.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//  - Split into one specification per format on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor And Settings Formats

Mason's own file formats. Each has its own specification; this page lists
them and the rules they share.

| File | Schema | Holds | Spec | Code | Tests |
| --- | --- | --- | --- | --- | --- |
| `.cymap` + `.cymapchunk` | `cypher.map` / `cypher.map_chunk` V10 | a whole map: every entity, brush, mesh, patch, terrain, material table, group, prefab instance | [CYMAP.md](CYMAP.md) | `CypherMap_Document` | `tests/CypherEditor/Workspaces/Map/Core` |
| `.cygame` | `cypher.game` V1 | a game's entity definitions, game map settings, editor defaults | [CYGAME.md](CYGAME.md) | `CypherEditor_GameProfile` (planned) | `tests/CypherEditor/Core/Project` |
| `.cytheme` | `cypher.theme` V2 (reads V1) | every colour, font, size, and style choice of the editor | [CYTHEME.md](CYTHEME.md) | `CypherEditor_Theme`, `CypherEditorGui_Style` | `tests/CypherEditor/Core/Themes`, `tests/CypherEditor/Gui` |
| `.cykeymap` | `cypher.editor_keymap` V2 (reads V1) | every shortcut, held key, and mouse gesture per editor context | [CYKEYMAP.md](CYKEYMAP.md) | `CypherEditor_Keymap`, `CypherEditorGui_Actions` | `tests/CypherEditor/Core/Commands`, `tests/CypherEditor/Gui` |
| `.cylayout` | `cypher.layout` V2 (reads V1) | the whole window arrangement: docks, toolbars, viewport grid, panel state | [CYLAYOUT.md](CYLAYOUT.md) | `CypherEditor_Layout`, `CypherEditorGui_Docking` | `tests/CypherEditor/Core/Layouts`, `tests/CypherEditor/Gui` |
| `.cyworkspace` | `cypher.workspace` V2 (reads V1) | one developer's session: personal settings, live layout, open documents, per-map cameras and visibility | [CYWORKSPACE.md](CYWORKSPACE.md) | `CypherEditor_Workspace` | `tests/CypherEditor/Core/Project` |
| `.cyproject` | `cypher.project` V3 (reads V1-V2) | a game: content roots, game profile, team settings and resources, build, cook, run | [CYPROJECT.md](CYPROJECT.md) | `CypherCommon_ProjectManifest` | `tests/CypherCommon/Tier2/*ProjectManifest*` |
| `.cysettings` | `cypher.settings` V2 (reads V1) | engine and editor settings; the full editor settings catalogue | [CYSETTINGS.md](CYSETTINGS.md) | `CypherCommon_Settings`, `CypherCommon_SettingsDocument` | `tests/CypherCommon/Tier2/*Settings*` |
| `.cyfont` | `cypher.font` V2 (reads V1) | a font family for editor and game: faces, rendering, glyph coverage, fallbacks | [CYFONT.md](CYFONT.md) | `CypherCommon_FontDefinition` | `tests/CypherCommon/Tier2/*FontDefinition*` |
| `.cyplugin` | `cypher.plugin` V1 | a Python editor plugin's manifest | [CYPLUGIN.md](CYPLUGIN.md) | `CypherEditor_PluginManifest` (planned) | `tests/CypherEditor/Core/Scripting` |

Version numbers above are the specified contract. Where code still decodes an
older version, the spec's "Versions" section says what the new version adds;
[FORMAT_CATALOG.md](FORMAT_CATALOG.md) tracks implementation status.

All files are CYKV language 1. In CYKV, array elements are separated by
commas; object members are not.

**Complete versus sparse.** Themes, keymaps, and layouts saved by Mason are
complete: every registered token, command, or member is written, so the file
documents everything and can be edited by hand. Settings are sparse by design
because they layer (workspace, project, user, default); see
[CYSETTINGS.md](CYSETTINGS.md).

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
