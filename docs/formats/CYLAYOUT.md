<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYLAYOUT.md
//  Purpose: Specifies editor layouts (`.cylayout`, cypher.layout V2): the
//           whole arrangement of an editor window - window placement, docked
//           and floating panels, toolbars, the viewport grid, and panel
//           state - saved as named profiles.
//  Details: Identity in ADR 0009; layouts are docking-library independent
//           (ADR 0008). Source and tests are authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V2 with window, toolbars, views, panels, and hidden placements)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor Layouts (`.cylayout`, V2)

A layout is how you have arranged the editor: where the console is, which
panels share a tab group, which toolbars are showing and where, whether the
viewports are a four-way grid or one big 3D view and what each pane shows,
how wide the outliner's columns are, and where the window sits. Rearrange
Mason at runtime, save it as a layout, and switch between layouts like
profiles ("Mapping", "Lighting", "Scripting", "Laptop").

## 1. Files and scopes

| Scope | Location | Use |
| --- | --- | --- |
| Built-in | editor resources (`mason_default.cylayout`, `mason_single_view.cylayout`, ...) | presets; "Reset Layout" returns to the workspace kind's default |
| User | `<config>/Mason/layouts/<id>.cylayout` | your profiles, on every project |
| Project | `<project>/editor/layouts/<id>.cylayout` | team layouts, committed |
| Workspace | the workspace's `state.layout` member (same content, embedded) | the live arrangement, saved automatically on exit and restored on start |

"Save Layout As" writes a user layout (or a project layout, when chosen).
The layout menu lists every scope, grouped. The setting `editor.ui.layout`
picks the layout a new workspace starts with.

## 2. Document

```cykv
@cykv 1
@schema "cypher.layout" 2
{
    id = "mapping"
    name = "Mapping"
    author = "Karlo"
    description = "Four views, console under them, properties full height."
    workspace = "map"

    window = { x = 40 y = 30 width = 2400 height = 1320 maximized = true fullscreen = false screen = "DELL U2720Q" }

    root = {
        split = "horizontal"
        sizes = [ 0.78, 0.22 ]
        children = [
            { split = "vertical" sizes = [ 0.8, 0.2 ] children = [
                { tabs = [ "mason.views" ] },
                { tabs = [ "console", "map.problems" ] current = 0 }
            ] },
            { split = "vertical" sizes = [ 0.45, 0.55 ] children = [
                { tabs = [ "map.outliner", "map.layers" ] },
                { tabs = [ "map.properties" ] }
            ] }
        ]
    }
    floating = [ { x = 120 y = 120 width = 520 height = 640 root = { tabs = [ "map.materials" ] } } ]
    hidden = [ { panel = "map.history" beside = "map.outliner" area = "right" } ]

    toolbars = [
        { id = "mason.file" area = "top" row = 0 order = 0 },
        { id = "mason.edit" area = "top" row = 0 order = 1 },
        { id = "map.grid" area = "top" row = 0 order = 2 },
        { id = "map.tools" area = "left" row = 0 order = 0 icon_size = 28 },
        { id = "map.render" area = "top" row = 1 order = 0 visible = false }
    ]
    status_bar = { visible = true }

    views = {
        arrangement = "quad"
        columns = [ 0.5, 0.5 ]
        rows = [ 0.55, 0.45 ]
        active = 3
        maximized = -1
        panes = [
            { view = "top" render = "wireframe" grid = true },
            { view = "front" render = "wireframe" grid = true },
            { view = "side" render = "wireframe" grid = true },
            { view = "perspective" render = "textured" grid = false }
        ]
    }

    panels = {
        "map.outliner" = { columns = [ 220, 80, 60 ] group_by = "layer" }
        "map.properties" = { mode = "smart" sort = "category" }
        console = { levels = [ "info", "warning", "error" ] }
    }
}
```

## 3. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier, 1-64 bytes |
| `name` | string | yes | display name, 1-128 bytes |
| `author`, `description` | string | no | at most 128 / 1024 bytes |
| `workspace` | string | no | workspace kind the layout is for (`map`, `material`, ...); empty means any |
| `window` | object | no | main window placement (below) |
| `root` | node | yes | the docked arrangement (below) |
| `floating` | array | no | floating windows: `x`, `y`, `width`, `height`, `root` (a node) |
| `hidden` | array | no | closed panels and where they return when reopened |
| `toolbars` | array | no | toolbar placement and visibility |
| `status_bar` | object | no | `visible` (bool, default true) |
| `views` | object | no | the central viewport grid of workspaces that have one |
| `panels` | object | no | panel ID to that panel's own state object |

**`window`:** `x`, `y`, `width`, `height` (logical pixels; at least 320 by
240), `maximized`, `fullscreen` (bools), `screen` (the display's name; when
that display is missing the window opens on the primary display, clamped to
it).

**Nodes.** A node is a split (`split` = `horizontal` / `vertical`,
`children`, optional `sizes`) or a tab group (`tabs`, a list of panel IDs,
optional `current` index). Sizes are relative weights normalised per split;
missing or mismatched sizes become equal shares. The workspace's central
panel (`mason.views` for the map workspace) must appear in `root` exactly
once.

**`hidden`:** `panel` (panel ID), `beside` (the panel it was docked with, or
tabbed with when `tabbed = true`), `area` (`left`, `right`, `top`, `bottom`,
`floating`) as the fallback when `beside` is not showing.

**`toolbars`:** `id` (toolbar ID registered by a module), `area` (`top`,
`bottom`, `left`, `right`, `floating`), `row` (0 is nearest the edge),
`order` (position in the row), `visible` (default true), `icon_size`
(overrides the theme's size for that toolbar), `x`/`y` when floating, and
optional `items`: a list of command IDs and `"-"` separators replacing the
toolbar's default buttons. Toolbars a build does not register are kept.

**`views`:**

| Member | Values | Default |
| --- | --- | --- |
| `arrangement` | `single`, `columns` (side by side), `rows` (stacked), `quad`, `three_left`, `three_right`, `three_top`, `three_bottom`, `three_columns`, `three_rows`, `four_left`, `four_right`, `four_top`, `four_bottom`, `four_columns`, `four_rows`. `three_left` is one large pane on the left beside two stacked; `three_top` one large pane above two; `four_*` the same with three; panes fill in reading order | `quad` |
| `columns`, `rows` | relative weights of the grid's columns and rows | equal |
| `active` | pane index with keyboard focus | 0 |
| `maximized` | pane index shown alone, or -1 | -1 |
| `panes[].view` | `top`, `front`, `side`, `perspective`, `bottom`, `back`, `left`, `uv` | per arrangement |
| `panes[].render` | `wireframe`, `flat`, `textured`, `lit`, `lighting_only`, `normals` | `wireframe` in 2D, `textured` in 3D |
| `panes[].grid` | bool | true in 2D, false in 3D |
| `panes[].show` | list of overlay names: `entities`, `entity_names`, `helpers`, `io`, `models`, `decals`, `terrain`, `patches`, `cordon` | all but `entity_names` |

Cameras are not part of a layout: where each view looks belongs to the map
being edited and is kept per map in the workspace
([CYWORKSPACE.md](CYWORKSPACE.md)).

**`panels`:** each panel owns its object and writes only what it needs to
restore (column widths, sort, filters, expanded groups, split positions).
Objects of panels a build does not register are kept.

## 4. Applying and capturing

- Panel IDs no registered panel answers to (a plugin that is not loaded) are
  skipped when showing the layout and kept when saving it.
- Structural damage in `root` (no central panel, a node that is neither a
  split nor a tab group, depth or node limits) rejects the layout and the
  editor keeps its current one; every other invalid member falls back to its
  default and is reported.
- Capturing writes every member above from the live window, so a saved
  layout restores the arrangement completely.
- The Qt dock-area implementation (EditorGui Docking) shows side columns,
  bottom and top strips, tab groups, and stacked panels exactly; trees it
  cannot express are shown by the closest arrangement and saved unchanged
  until the Qt Advanced Docking System implementation replaces it.

## 5. Limits

256 nodes, depth 16, 64 children or tabs per node, 32 floating windows, 128
toolbars, 16 view panes, 16 MiB per file.

## 6. Versions

V1 (2026-09-25) had `id`, `name`, `workspace`, `root`, and `floating`. V2
adds `author`, `description`, `window`, `hidden`, `toolbars`, `status_bar`,
`views`, and `panels`; it reads V1 unchanged and saving writes V2.
