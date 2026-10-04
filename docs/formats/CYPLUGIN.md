<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYPLUGIN.md
//  Purpose: Specifies Python editor plugin manifests (`.cyplugin`,
//           cypher.plugin V1): identity, compatibility, what the plugin adds
//           to the editor, and what it asks permission to do.
//  Details: Identity in ADR 0009; plugins act only through commands
//           (ADR 0008). Source and tests are authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    with requirements, contributions, and permissions)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor Plugins (`.cyplugin`, V1)

A plugin is a folder holding a `.cyplugin` manifest and its Python modules.
The manifest lets Mason list the plugin, show what it adds before loading it,
check compatibility, and ask the user to trust it with what it needs.

## 1. Locations

Built-in (editor install), user (`<config>/Mason/plugins/`), project (the
project's `editor.plugins` folder), workspace (`<workspace>/plugins/`). A
project or workspace plugin runs only after the user trusts it; trust is
recorded in the user settings with the manifest's hash, so an edited manifest
asks again.

## 2. Document

```cykv
@cykv 1
@schema "cypher.plugin" 1
{
    id = "prefab_scatter"
    name = "Prefab Scatter"
    version = "1.2.0"
    api = 1u
    entry = "prefab_scatter"
    author = "Karlo"
    description = "Scatter prefabs over selected faces."
    homepage = "https://example.com/prefab_scatter"
    license = "MIT"
    icon = "icons/scatter.svg"
    editor = { min_version = "0.1.0" }
    platforms = [ "macos", "windows", "linux" ]
    requires = [ { id = "geometry_tools" version = ">=2.0.0" } ]

    commands = [ "prefab_scatter.run", "prefab_scatter.settings" ]
    panels = [ { id = "prefab_scatter.panel" title = "Scatter" } ]
    settings = [ "editor.prefab_scatter" ]
    theme_tokens = [ "prefab_scatter.preview" ]
    keymap = "keys.cykeymap"
    workspaces = [ "map" ]

    permissions = [ "read_project", "write_documents" ]
}
```

## 3. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier; also the module prefix of everything it registers |
| `name` | string | yes | display name |
| `version` | string | yes | `major.minor.patch` |
| `api` | u64 | yes | editor plugin API version it was written for; a newer API than the editor's refuses to load |
| `entry` | string | yes | Python module or package to import, relative to the plugin folder |
| `author`, `description`, `homepage`, `license` | string | no | shown in the plugin manager (`license` an SPDX ID) |
| `icon` | string | no | SVG in the plugin folder |
| `editor` | object | no | `min_version`: oldest Mason it supports |
| `platforms` | array | no | `macos`, `windows`, `linux`; default all |
| `requires` | array | no | other plugins: `id` and a `version` range (`>=2.0.0`, `^1.4`, `1.2.x`) |
| `commands` | array | no | commands it registers, listed so menus and keymaps can show them before it loads; each starts with `<id>.` |
| `panels` | array | no | panels it adds: `id` (starts with `<id>.`), `title` |
| `settings` | array | no | settings sections it registers |
| `theme_tokens` | array | no | theme tokens it registers |
| `keymap` | string | no | a `.cykeymap` in the plugin folder with default bindings for its commands; merged under the built-in keymap |
| `workspaces` | array | no | workspace kinds it works in; default all |
| `permissions` | array | no | what the trust prompt shows (below) |

**Permissions:** `read_project` (read project files), `write_documents`
(change open documents, always through commands, so undoable),
`write_files` (write files in the project outside documents), `run_tools`
(start build tools and processes), `network` (network access). The editor
enforces `write_documents` through the command system; the others are
declarations shown to the user, because Python code runs with the editor's
rights.

## 4. Limits

256 commands, 64 panels, 64 requirements, 16 MiB per file.
