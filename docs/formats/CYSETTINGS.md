<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYSETTINGS.md
//  Purpose: Specifies settings documents (`.cysettings`, cypher.settings V2)
//           and the complete editor settings catalogue: every Mason
//           preference with its type, default, limits, and settings page.
//  Details: Identity and scopes in ADR 0009; settings are registered by the
//           modules that own them (ADR 0008). The catalogue is the plan of
//           record for the generated settings dialog. Source and tests are
//           authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    with the editor settings catalogue)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Settings (`.cysettings`, V2)

Settings are the choices that change how the engine and the editor behave:
autosave, camera speed, grid snapping, texture lock, how many undo steps,
which theme, keymap, and layout. How things *look* is the theme
([CYTHEME.md](CYTHEME.md)); which keys do what is the keymap
([CYKEYMAP.md](CYKEYMAP.md)); where panels are is the layout
([CYLAYOUT.md](CYLAYOUT.md)).

## 1. Scopes and files

| Scope | Where the values live | Shared |
| --- | --- | --- |
| Workspace | the workspace's `settings` member | no |
| Project | the project's `settings` member | committed |
| User | `<config>/Mason/editor.cysettings` (editor), `<config>/<game>/settings.cysettings` (game) | no |
| Default | the registered descriptor | built in |

A value resolves from the first scope that holds a valid one. **Settings files
are sparse on purpose:** writing a value into a scope removes the scope's own
entry when it equals what the wider scopes already resolve to. A complete user
file would override every project setting and freeze old defaults forever;
sparse files let a team set `editor.grid.size = 8` for everyone and let
improved defaults reach users. The settings dialog always shows every
setting, marks where each value comes from, and can reset a value to inherit.
"Export Settings" writes every resolved value into a complete file for backup
or sharing; importing it writes only the differences.

### 1.1 Settings transfer in Mason

Settings exposes **Import Settings…** and **Export Settings…** beside its Close
button. Export writes a portable `.cysettings` V2 document containing every
registered effective value, including defaults, and unknown members merged
through the attached scope stack. It uses an atomic file replacement and does
not alter the editor's live settings. Because this is a complete snapshot,
importing it deliberately records the choices of that snapshot; exporting does
not turn the ordinary sparse user file into a complete file.

Import targets the dialog's explicit **Save to** scope, with User selected by
default. It first shows a review of each known incoming setting, its current
value, and any Project or Workspace value that will continue to control it.
Unmentioned settings stay as they are, unknown members merge without discarding
their siblings, and known incoming values are written sparsely against the
selected scope's inherited values. A malformed document or an invalid known
value rejects the transfer before any settings change. Cancelling the review
has no effect. A scope or value changed since review requires a fresh review.

The application supplies the selected scope's write permissions and atomic
persistence handler. Apply is unavailable for an absent, read-only, blocked, or
unsupported scope. The candidate is persisted before publication; persistence
or allocation failure leaves the existing settings intact. A successful import
publishes one registry change notification so the editor refreshes together.

`editor.ui.theme`, `editor.ui.keymap`, and `editor.ui.layout` are IDs referring
to separately installed files. Settings export includes these references, not
the contents of theme, keymap, or layout files. Appearance provides theme Save,
Save As, Import and Export; Keybindings provides its own keymap Import/Export.
Transfer those assets separately when sharing a customized setup.

## 2. Document

An open object of sections. Any section or member is allowed; owners read
theirs through descriptors, and members nobody registers are kept.

```cykv
@cykv 1
@schema "cypher.settings" 2
{
    display = { width = 2560 height = 1440 mode = "borderless" vsync = true }
    editor = {
        ui = { theme = "midnight" keymap = "karlo" layout = "mapping" }
        camera = { move_speed = 1400.0 invert_y = false }
        grid = { size = 8 }
        map = { texture_lock = true default_material = "materials/dev/dev_orange.cymat" }
    }
}
```

Values are checked one at a time by `setting_descriptor_t` (type, limits,
default, label, page). A read reports `VALUE`, `ABSENT`, or `INVALID`; an
invalid value never fails the file and falls back to the next scope. Reals
accept integers. Colours are `"#rrggbb"` / `"#rrggbbaa"`, lower case.

## 3. Engine settings

| Path | Type | Default | Limits |
| --- | --- | --- | --- |
| `display.width` | integer | 1280 | 320-16384 |
| `display.height` | integer | 720 | 200-16384 |
| `display.mode` | enum | `windowed` | `windowed`, `borderless`, `fullscreen` |
| `display.vsync` | bool | true | - |

`CypherSettings_Decode` reads V1 and V2; an invalid display value keeps its
default and is reported with a `/display/width` style path.

## 4. Editor settings catalogue

Page names are the settings dialog's tree. Types: bool, integer, real,
string, enum (one of the listed values), asset (canonical virtual path with
the listed extension).

### 4.1 General (`editor.general`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `language` | enum | `en` | installed interface languages | interface language |
| `autosave_minutes` | integer | 5 | 0-120 | autosave interval; 0 disables |
| `autosave_keep` | integer | 10 | 1-100 | autosave copies kept per document |
| `undo_levels` | integer | 500 | 10-100000 | undo steps kept per document |
| `recent_files` | integer | 16 | 0-64 | entries in recent lists |
| `restore_session` | bool | true | - | reopen the workspace's documents on start |
| `confirm_exit` | bool | true | - | ask before exiting with unsaved changes |
| `backups_on_save` | integer | 3 | 0-32 | `.bak` generations kept when saving documents |

### 4.2 Interface (`editor.ui`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `theme` | string | `charcoal` | theme ID | active theme |
| `keymap` | string | `cypher_default` | keymap ID | active keymap |
| `layout` | string | `mason_default` | layout ID | layout for new workspaces |
| `start_maximized` | bool | true | - | open the main window maximised |
| `show_splash` | bool | true | - | show the themed Mason startup screen during initialization |
| `two_toolbar_rows` | bool | true | - | normalize the upper toolbars to two rows after layout restore; disable for custom placements |
| `tree_lines` | bool | true | - | draw hierarchy connectors in the map outliner |
| `native_menu_bar` | bool | false | - | use the macOS system menu bar instead of the in-window menu bar |
| `tooltips` | bool | true | - | show tooltips |
| `tooltip_delay_ms` | integer | 600 | 0-5000 | delay before a tooltip |
| `command_palette_recent` | integer | 12 | 0-64 | recent commands shown first in the palette; 0 disables recent ordering |
| `command_palette_rows` | integer | 12 | 4-24 | visible palette rows before scrolling; matching results remain searchable |
| `command_palette_show_unavailable` | bool | true | - | include unavailable commands dimmed; false shows available commands only |
| `confirm_delete` | bool | false | - | ask before deleting objects |

`theme` takes effect as soon as it changes. Mason loads the user themes in
`<config>/Mason/themes` at start; an ID no installed theme has keeps the
current theme and logs a warning, and the setting is left alone so the theme
returns once its file does. Saving from the theme editor writes this setting, and so does choosing or saving a theme on the Appearance page of Settings (CYTHEME.md 1.1).

### 4.3 Viewports (`editor.viewport`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `activate_on_hover` | bool | true | - | the pane under the pointer receives keys without a click |
| `highlight_active` | bool | false | - | tint the active pane's header |
| `active_border` | bool | true | - | outline the pane under the pointer |
| `hover_highlight` | bool | true | - | draw the object under the pointer in `viewport.hover` (green) |
| `gizmo_scale` | real | 1.0 | 0.5-2.0 | screen-space scale of 2D/3D gizmos, preserving their original sizes at 1 |
| `show_axes` | bool | true | - | axis triad in each pane |
| `center_axes` | bool | true | - | triad at the origin rather than the corner |
| `show_rulers` | bool | true | - | coordinate rulers along 2D panes |
| `show_fps` | bool | false | - | frame time readout in the 3D pane |
| `show_metrics` | bool | false | - | camera coordinates, pixel scale and object counts inside viewports |
| `show_selection_bounds` | bool | true | - | bounding box around selected geometry |
| `show_selection_dimensions` | bool | true | - | selected geometry dimensions along world axes |
| `show_selection_vertices` | bool | false | - | vertex markers on selected geometry |
| `antialiasing` | enum | `4x` | `off`, `2x`, `4x`, `8x` | multisampling |
| `texture_filter` | enum | `anisotropic` | `nearest`, `linear`, `anisotropic` | 3D texture filtering |
| `max_fps` | integer | 0 | 0-1000 | frame cap; 0 follows the display |
| `near_plane` | real | 1.0 | 0.01-64 | 3D near clip (units) |
| `far_plane` | real | 32768.0 | 256-1048576 | 3D far clip (units) |
| `entity_names` | enum | `selected` | `never`, `selected`, `always` | when entity names are drawn |
| `io_lines` | enum | `selected` | `never`, `selected`, `always` | when output connections are drawn |
| `helpers` | enum | `selected` | `never`, `selected`, `always` | when radius and cone helpers are drawn |
| `models_in_2d` | bool | true | - | draw model wireframes in 2D |
| `ghost_hidden` | bool | false | - | draw hidden layers dimmed instead of not at all |

### 4.4 Camera (`editor.camera`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `fov` | real | 75.0 | 20-130 | 3D field of view (degrees) |
| `move_speed` | real | 1000.0 | 10-100000 | fly speed (units per second) |
| `acceleration` | real | 0.15 | 0-2 | seconds to reach full speed; 0 is instant |
| `fast_multiplier` | real | 4.0 | 1-100 | held `map.camera.fast` |
| `slow_multiplier` | real | 0.25 | 0.01-1 | held `map.camera.slow` |
| `look_sensitivity` | real | 0.2 | 0.01-5 | degrees per pixel |
| `pan_sensitivity` | real | 1.0 | 0.05-10 | pan speed scale |
| `zoom_sensitivity` | real | 1.0 | 0.05-10 | wheel zoom scale |
| `invert_y` | bool | false | - | invert vertical look |
| `invert_wheel` | bool | false | - | invert wheel zoom |
| `zoom_to_cursor` | bool | true | - | zoom toward the pointer |
| `orbit_selection` | bool | true | - | orbit around the selection when one exists |
| `link_2d_views` | bool | false | - | 2D panes share pan and zoom |
| `frame_margin` | real | 1.15 | 1.0-2.0 | padding when framing selection or all geometry |

Mason's current viewports consume `pan_sensitivity` in every orthographic view
and in perspective pan. Sensitivity is captured when the gesture starts, so
editing it during a pan affects the next gesture without moving the current
one discontinuously. `gizmo_scale` changes drawing and picking together; changing
it cancels a captured geometry edit rather than committing a jump. `frame_margin`
is read on the next framing request and does not move the camera merely because
the preference changes. These settings use the existing scope persistence and
settings import/export flow.

### 4.5 Grid and snapping (`editor.grid`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `size` | integer | 16 | 1-4096 | grid size (units); `[` / `]` halve and double it |
| `snap` | bool | true | - | snap to the grid |
| `show` | bool | true | - | draw the 2D grid |
| `show_3d` | bool | true | - | draw the 3D floor grid independently of world axes |
| `adaptive` | bool | true | - | hide levels whose lines would be closer than `min_spacing_px` |
| `min_spacing_px` | integer | 4 | 2-128 | closest line spacing drawn (pixels) |
| `major_every` | integer | 8 | 2-64 | major interval in authored grid cells; adaptive rendering retains intervening minor lines |
| `highlight_every` | integer | 1024 | 0-65536 | highlight lines every N units; 0 disables |
| `angle_snap` | real | 15.0 | 0-90 | rotation snap (degrees); 0 disables |
| `scale_snap` | real | 0.25 | 0-10 | scale snap step; 0 disables |

### 4.6 Map editing (`editor.map`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `default_material` | asset `.cymat` | `materials/dev/dev_grid.cymat` | - | material of new brushes |
| `texture_lock` | bool | true | - | textures move with brushes |
| `texture_scale_lock` | bool | false | - | textures scale with brushes |
| `default_texture_scale` | real | 0.25 | 0.001-64 | texture scale of new faces |
| `new_brush_shape` | enum | `box` | `box`, `wedge`, `cylinder`, `spike`, `sphere`, `arch` | block tool shape |
| `cylinder_sides` | integer | 16 | 3-128 | sides of new cylinders and spikes |
| `clip_mode` | enum | `both` | `both`, `front`, `back` | what the clipping tool keeps |
| `carve_mode` | enum | `slice` | `slice`, `precise` | how carve splits brushes |
| `hollow_thickness` | real | 16.0 | 1-1024 | wall thickness for Hollow (units) |
| `select_created` | bool | true | - | select newly created objects |
| `resize_from_center` | bool | false | - | bounds handles resize around the captured center; otherwise the opposite side stays fixed. Shift at handle press temporarily selects the center. The dragged boundary snaps and the opposite boundary mirrors it, retaining an off-grid center exactly. |
| `paste_offset` | enum | `grid` | `none`, `grid`, `cursor` | where pasted objects land |
| `default_point_class` | string | `info_player_start` | class name | entity tool default |
| `default_solid_class` | string | `func_detail` | class name | "Tie to Entity" default |
| `write_all_properties` | bool | true | - | new entities store every class property (CYMAP.md 4) |
| `check_before_run` | bool | true | - | run "Check for Problems" before running a map |
| `cell_size` | real | 8192.0 | 0-262144 | chunk cell size for new maps (units) |

### 4.7 Files (`editor.files`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `watch_external_changes` | bool | true | - | reload documents changed on disk after asking |
| `line_endings` | enum | `lf` | `lf`, `crlf` | line endings of written text files |
| `save_on_run` | bool | true | - | save the map before building or running it |

`editor.assets.roots` (string, default empty, at most 1024 bytes; shown on
the Files page as "Content folders") lists the folders the asset browser
scans, highest priority first, separated by `;`. Relative folders resolve
against the project folder: the nearest folder above the open map that holds
an `assets` folder or a `.cyproject` file. Empty uses that project's `assets`
folder. Where two folders give the same virtual path, the first wins, as
mounting does.

### 4.8 Console (`editor.console`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `history_size` | integer | 200 | 0-10000 | remembered command lines |
| `max_lines` | integer | 5000 | 100-1000000 | lines kept in the output |
| `min_level` | enum | `info` | `trace`, `debug`, `info`, `warning`, `error` | lowest level shown |
| `timestamps` | bool | false | - | prefix lines with the time |
| `channels` | bool | true | - | prefix lines with the log channel |

### 4.9 Performance (`editor.performance`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `worker_threads` | integer | 0 | 0-256 | background threads; 0 chooses from the CPU |
| `texture_memory_mb` | integer | 2048 | 128-65536 | texture cache budget |
| `background_build` | bool | true | - | build maps without blocking editing |

### 4.10 Plugins (`editor.plugins`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `enabled` | bool | true | - | load Python plugins at all |
| `trusted` | list | empty | - | trusted plugin locations with their manifest hash (written by the trust prompt, user scope only) |

Workspaces register more pages as they arrive (material editor, particle
editor, ...), under their own `editor.<workspace>` sections.

### 4.11 Asset presentation (`editor.assets`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `thumbnail_size` | integer | 144 | 48-256 | material and asset preview size in logical pixels; the browser slider and size presets update this setting |
| `list_view` | bool | false | - | show assets in a list; switching back to the grid retains its thumbnail size |
| `saved_searches` | string | empty | 8192 bytes | the Asset Browser window's saved searches as a JSON array of `{ name, filter, tab, types, sources }`; written by the window |

Both settings take effect immediately and persist in the selected settings
scope. The browser keeps filtering and the current asset when changing views.

### 4.12 Outliner (`editor.outliner`)

| Key | Type | Default | Limits | Meaning |
| --- | --- | --- | --- | --- |
| `show_ids` | bool | false | - | append IDs to named mapnodes; unnamed nodes retain an ID so they remain distinguishable |

The outliner filter menu exposes the ID preference. Hierarchy connectors are
controlled by `editor.ui.tree_lines` in Interface settings.
Type, entity class, and ID remain in each mapnode's tooltip when IDs are hidden.

### 4.13 Sound preview (`editor.audio`)

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `preview_enabled` | bool | true | enable editor audio previews when an audio backend is available |

The top-right sound button and settings dialog share this preference. The
current editor has no audio preview backend; toggling the preference does
not start playback.

## 5. Limits

16 MiB per file; paths nest at most 8 segments of 96 bytes.

## 6. Versions

V1 files are valid V2 content. V2 is current.
