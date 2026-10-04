<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYWORKSPACE.md
//  Purpose: Specifies workspaces (`.cyworkspace`, cypher.workspace V2): one
//           developer's working area on one machine - personal settings,
//           the live layout, open documents, and per-map personal state such
//           as cameras, bookmarks, and what is hidden.
//  Details: Identity and scopes in ADR 0009. A workspace is never committed
//           to the project and never packaged. Source and tests are
//           authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V2 with documents, per-map state, console, plugins, build and run)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Workspaces (`.cyworkspace`, V2)

A workspace is where you work: it points at a project, overlays your own
work-in-progress content, holds your personal settings, and remembers
everything about your session so Mason reopens exactly as you left it - the
layout, the open maps, where every camera was looking, which layers you had
hidden, which tool was active, and what you typed in the console.

Anything personal lives here and never in the map: two people editing one
map each keep their own cameras, hidden objects, and selection.

## 1. File

`<workspace folder>/<name>.cyworkspace`, beside the workspace's content
overlay. Mason keeps a recent-workspaces list in the user scope. The file is
saved on exit, on "Save All", and every `editor.general.autosave_minutes`
with the documents.

## 2. Document

```cykv
@cykv 1
@schema "cypher.workspace" 2
{
    id = "7b0e2c5a-9f41-4c8e-b3a2-5d1f0e6c9a77"
    name = "main"
    owner = "Karlo"
    project = "../reap/reap.cyproject"
    content = "wip"

    settings = {
        editor = { general = { autosave_minutes = 5 } camera = { move_speed = 1200.0 } }
    }

    state = {
        layout = { id = "mapping" name = "Mapping" workspace = "map" root = { ... } views = { ... } }
        documents = [
            { path = "maps/facility.cymap" kind = "map" },
            { path = "materials/concrete/floor01.cymat" kind = "material" }
        ]
        active_document = 0
        recent = {
            maps = [ "maps/facility.cymap", "maps/arena_test.cymap" ]
            files = [ "scripts/waves.cyscript" ]
        }
    }

    maps = {
        "maps/facility.cymap" = {
            cameras = [
                { pane = 0 center = [ 512.0, -256.0 ] zoom = 0.5 },
                { pane = 1 center = [ 512.0, 64.0 ] zoom = 0.5 },
                { pane = 2 center = [ -256.0, 64.0 ] zoom = 0.5 },
                { pane = 3 position = [ 820.0, -1400.0, 420.0 ] angles = [ 18.0, 95.0, 0.0 ] fov = 75.0 }
            ]
            bookmarks = [
                { name = "Spawn room" position = [ 128.0, -64.0, 96.0 ] angles = [ 10.0, 90.0, 0.0 ] },
                { name = "Wave 3 arena" position = [ 2048.0, 1024.0, 512.0 ] angles = [ 35.0, 225.0, 0.0 ] }
            ]
            hidden = { objects = [ 2201u, 2202u ] layers = [ "notes" ] visgroups = [ 13u ] }
            locked = { layers = [ "structure" ] }
            current_layer = "gameplay"
            selection = [ 101u, 102u ]
            tool = "map.tool.select"
            grid = { size = 16 snap = true }
            cordon = 0
            material = "materials/concrete/floor01.cymat"
            entity_class = "info_wave_start"
        }
    }

    console = { history = [ "map.check", "console.help map.grid" ] }
    plugins = { enabled = [ "prefab_scatter" ] disabled = [ "old_importer" ] }
    build = { profile = "fast" }
    run = { configuration = "play_windowed" }
}
```

## 3. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | nonzero UUID; new workspaces get a random one |
| `name` | string | yes | 1-128 bytes |
| `project` | string | yes | native path to the `.cyproject`, relative to the workspace file, 1-1024 bytes |
| `owner` | string | no | display name, at most 128 bytes |
| `content` | string | no | relative path inside the workspace, no `..`, no root or drive; overlays project content |
| `settings` | object | no | personal settings scope ([CYSETTINGS.md](CYSETTINGS.md)); resolves before project and user |
| `state` | object | no | session state (below) |
| `maps` | object | no | canonical map path to that map's personal state (below) |
| `console` | object | no | `history`: up to `editor.console.history_size` lines, oldest first |
| `plugins` | object | no | `enabled`, `disabled`: plugin IDs overriding the project and user choice for this workspace |
| `build` | object | no | `profile`: last used build profile ID from the project |
| `run` | object | no | `configuration`: last used run configuration ID from the project |
| `sharing`, `automation` | - | reserved | peer sharing and MCP policy (planned); preserved untouched |

**`state`:** `layout` (a complete layout, the same members as a `.cylayout`
root, [CYLAYOUT.md](CYLAYOUT.md)), `documents` (open documents in tab order:
`path` canonical virtual path, `kind` workspace kind), `active_document`
(index), `recent` (`maps`, `files`: canonical paths, newest first, up to
`editor.general.recent_files`).

**Per-map state (`maps.<path>`):**

| Member | Type | Rules |
| --- | --- | --- |
| `cameras` | array | per view pane: `pane` index; 2D panes `center` `[a, b]` in the view's plane and `zoom` (screen pixels per unit); the 3D pane `position`, `angles` `[pitch, yaw, roll]` in degrees, `fov` in degrees |
| `bookmarks` | array | named 3D camera positions: `name`, `position`, `angles`, optional `fov` |
| `hidden` | object | `objects` (map object IDs), `layers` (layer IDs), `visgroups` (visgroup IDs) hidden by this user |
| `locked` | object | `layers`, `objects`: not selectable for this user |
| `current_layer` | string | layer that receives new objects |
| `selection` | array | object IDs selected when the map was closed; restored when all still exist |
| `tool` | string | active tool command ID |
| `grid` | object | `size` (units), `snap` (bool); override the settings for this map |
| `cordon` | integer | index of the map's active cordon, or -1 |
| `material` | string | current material for new brushes and "Apply Current Material" |
| `entity_class` | string | class the entity tool places |

Map state whose map no longer exists is dropped when the workspace is saved;
IDs that no longer exist in the map are ignored and dropped the same way.

## 4. Validation

Identity failures (`id`, `name`, `project`) reject the workspace. Every other
member that is invalid is dropped from the live state and reported through
`workspace_problem_flags_t`; unknown members are kept.

## 5. Limits

512 documents, 1,024 maps with state, 256 bookmarks and 16 cameras per map,
65,536 hidden or selected IDs per map, 16 MiB per file.

## 6. Versions

V1 (2026-09-25) had `id`, `name`, `owner`, `project`, `content`, `settings`,
and a free-form `state`. V2 defines `state`, `maps`, `console`, `plugins`,
`build`, and `run`; V1 files read unchanged (their `state` members are kept)
and saving writes V2.
