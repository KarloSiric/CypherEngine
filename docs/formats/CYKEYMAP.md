<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYKEYMAP.md
//  Purpose: Specifies editor keymaps (`.cykeymap`, cypher.editor_keymap V2):
//           every editor shortcut - key chords, held keys, and mouse
//           gestures - per editor context, and Mason's built-in keymap.
//  Details: Identity in ADR 0009; commands-first rule in ADR 0008. The
//           built-in keymap below doubles as Mason's command catalogue: the
//           plan of record for which commands exist and where. Source and
//           tests are authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V2 with held keys, mouse gestures, platforms, and the full catalogue)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor Keymaps (`.cykeymap`, V2)

A keymap is every way the keyboard and mouse drive the editor: shortcuts that
run commands, keys held to fly the camera, and mouse gestures that look,
pan, zoom, and select. Game input is a separate family (`.cyinput`,
`.cybindings`, [Input Actions](INPUT_ACTIONS.md)); the schema IDs keep them
apart.

## 1. Files and scopes

| Scope | Location |
| --- | --- |
| Built-in | editor resources: `cypher_default.cykeymap` (Hammer-style); later presets such as `trenchbroom.cykeymap`, `radiant.cykeymap` |
| User | `<config>/Mason/keymaps/<id>.cykeymap` |
| Project | `<project>/editor/keymaps/<id>.cykeymap` |

The active keymap is the setting `editor.ui.keymap`. Settings > Keybindings
presents keymaps as named **Profiles**. It edits an owned working copy and
activates it only when **Save and activate profile** is chosen. Editing the
bundled default creates a sparse user keymap with the
default as its `base`; resetting a row removes its override so it inherits
again. Editing an existing or imported document retains its authored base,
platform overlays, metadata and unknown members rather than flattening it.
An empty trigger array is an explicit unbinding. Complete root keymaps and
partial keymaps with a base are both supported.

The page writes the selected keymap ID to the User settings scope. A Project
or Workspace override of `editor.ui.keymap` still has higher precedence; the
page reports its controlling ID after saving instead of claiming that the
new document controls input. The keymap file remains saved even in that case.

### Using Settings > Keybindings

The **Profile** selector lists installed profiles by name. **New Profile**
asks for a profile name, unique file ID and an installed starting profile;
the draft inherits that profile and stores only its own overrides.
**Duplicate Profile** copies the current working document, including staged
binding changes, authored base, platform metadata and unknown records, under
a new name and ID. Both create a staged draft: they neither activate input
nor create a file until **Save and activate profile**. Existing IDs and files
cannot be overwritten through the new/duplicate flow. The original profile
stays available in the selector. The **Shortcuts reference** button opens the
host's current shortcut reference without changing the staged profile.

The action browser groups Viewports, Camera, Selection, Transforms, Geometry
tools, Meshes, Materials, objects, organisation and general editor commands.
Search matches action names, IDs, keys and contexts. The platform selector
edits common declarations or a macOS, Windows or Linux overlay; Origin shows
whether the displayed value belongs to this document or its inherited base.

Select an action, edit up to four alternative triggers, and choose **Set
binding**. **Record** captures keyboard or held-key input. Keyboard alternatives
may contain a sequence such as `Ctrl+K, Ctrl+C`; a held action takes one key or
a modifier alone, such as `W` or `Shift`. Mouse examples are `RightDrag`,
`Alt+LeftDrag`, `Space+LeftDrag`, `Ctrl+LeftClick` and `Wheel`. Mouse text uses
the grammar in this document. **Unbind** writes `[]`; **Reset to inherited**
removes the selected declaration's override. These edits are staged until
**Save and activate profile**; **Discard changes** restores the active document. Closing Settings
with unapplied edits asks to apply, discard or cancel, including after
navigating to another settings page.

Apply checks all supported platform overlays for collisions within each
context and input kind, including keyboard sequence prefixes and mouse wheel
axis/direction overlap. It also checks registered window actions after their
`map` then `global` resolution: those Qt shortcuts coexist, so distinct
commands in those contexts must not produce the same chord or a sequence
prefix collision. Viewport/tool contexts may intentionally reuse input with
their routing priority.
Selection-box and transform drag deliberately share the viewport's gesture;
the clicked geometry and active tool decide that route. A declaration never
enables an unavailable command. Unregistered commands, commands disabled in
the current state, and multi-stroke view/tool declarations that the viewport
does not yet dispatch are labelled separately. The current remappable mouse
routes are 3D camera drags and vertical dolly wheel gestures. Geometry selection
and transformation, orthographic pan/zoom and context-menu mouse declarations
are marked **Not remappable yet**: those handlers still use fixed input.
Unknown held/mouse declarations are retained without claiming a working route.

**Import profile** stages a valid `.cykeymap` document without activating it.
**Export portable** writes one self-contained profile: the complete base chain
is resolved for common, macOS, Windows and Linux keyboard, held-key and mouse
declarations, including explicit unbindings and staged changes, then `base`
is removed. A missing, cyclic or excessively deep dependency chain prevents
export before the destination is opened. Malformed authored declarations are
also rejected: startup can fall back through a base when a declaration is
invalid, so retaining it while removing the base would lose that working
fallback. Correct the declarations or use source export to preserve them.
This profile's metadata, unknown
action records and unknown platform members are retained. Unknown platforms'
inheritance is not resolved; metadata from inherited base documents is not
merged into the exported profile.

**Export source** writes the authored working document with its base and
unknown content intact. Its status names the required base profiles, which
the recipient must install as well. Use this option to retain a sparse
profile's inheritance relationship; use portable export to share bindings
without dependencies. Export never activates the profile or clears staged
changes. Both export modes use the existing Cypher format without a schema
change; foreign Hammer or TrenchBroom configuration files are
not translated. The bundled default contains Mason's documented Hammer-style
choices and conveniences, not a claim of exact parity with a particular
Hammer version. Loading the required base documents is necessary before
applying a map that references them. Saves publish only after the file commits;
invalid input, allocation failures and failed writes retain the previous
working/live documents and destination file.

## 2. Document

```cykv
@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "karlo"
    name = "Karlo's keys"
    author = "Karlo"
    description = "Hammer layout, clip on C."
    base = "cypher_default"
    bindings = {
        global = { "file.save" = [ "Ctrl+S" ] "view.console" = [ "Backquote" ] }
        map = { "map.tool.clip" = [ "C" ] "map.tool.camera" = [] }
        "map.tool.vertex" = { "map.vertex.merge" = [ "Ctrl+M" ] }
    }
    held = {
        "map.viewport.3d" = { "map.camera.forward" = [ "W", "Up" ] "map.camera.fast" = [ "Shift" ] }
    }
    mouse = {
        "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] "map.camera.dolly" = [ "Wheel" ] }
        "map.viewport.2d" = { "map.view.pan" = [ "Space+LeftDrag", "MiddleDrag" ] }
    }
    platforms = {
        macos = { bindings = { global = { "edit.delete" = [ "Delete", "Backspace" ] } } }
    }
}
```

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier, 1-64 bytes |
| `name` | string | yes | display name, 1-128 bytes |
| `author`, `description` | string | no | at most 128 / 1024 bytes |
| `base` | string | no | keymap ID this one builds on; chains stop at 8 levels |
| `bindings` | object | no | context to (command ID to key chords); commands run once per press |
| `held` | object | no | context to (held action ID to keys); the action is active while a key is down |
| `mouse` | object | no | context to (mouse action ID to gestures) |
| `platforms` | object | no | `macos`, `windows`, `linux`: each may hold `bindings`, `held`, `mouse`, which apply over the main sections on that platform |

Command and action IDs are `module.name` lower-case stable identifiers. Each
lists up to 4 triggers; `[]` unbinds what a base or the built-in keymap
binds. IDs the running build does not register are kept and ignored, so a
keymap can name plugin commands or commands of a newer build.

## 3. Contexts

A context names where focus is. The editor keeps a **context stack**, most
specific first, and the first context whose binding matches wins:

```text
map.tool.vertex  >  map.selection.vertices  >  map.viewport.2d  >  map.viewport  >  map  >  global
```

| Context | Active when |
| --- | --- |
| `global` | always |
| `map` | a map document is the current document |
| `map.viewport` | a map viewport has focus |
| `map.viewport.2d` / `map.viewport.3d` | a 2D (top, front, side) or the 3D viewport has focus |
| `map.tool.<tool>` | that tool is active and a viewport has focus (`map.tool.clip`, `map.tool.vertex`, ...) |
| `map.selection.<mode>` | the active selection mode in a viewport (`vertices`, `edges`, `faces`, `meshes`, `objects`, `groups`, `navigation`); routing does not enable an unavailable mode |
| `outliner`, `properties`, `console`, `assets`, `history` | that panel has focus |
| `text_editor` | a script or text editor has focus |

Within one context the keymap chain decides (the keymap, then its bases, then
the built-in keymap), and conflict detection honours overrides. Keybindings
lists the contexts declared by the loaded documents. A registered command
without a declaration is initially offered in `map` for a `map.*` ID and
`global` otherwise; its context can be edited. Naming a context or action does
not itself implement its input handler.

A focused text field (property values, search boxes, the console input)
receives plain printable keys and editing keys before any shortcut, so
single-key bindings such as `H` (hide) or `U` (show all) never swallow typing.
Chords with `Ctrl`, `Alt`, or `Meta`, and function keys, still reach their
commands.

Viewport commands, logical gesture keys, camera mouse gestures, and held
navigation all use this stack. A disabled or explicitly unbound inherited
selection binding reserves its key before lower contexts, so future component
`Backspace` commands cannot fall through to whole-object deletion. Held flight
movement keeps priority over editing shortcuts while camera navigation is active.

## 4. Trigger syntax

**Key chords** (bindings). Modifiers `Ctrl`, `Alt`, `Shift`, `Meta` (aliases
Control, Option, Cmd, Command, Super, Win; any case), then one key: `A`-`Z`,
`0`-`9`, `F1`-`F24`, `Num0`-`Num9`, `NumAdd`, `NumSubtract`, `NumMultiply`,
`NumDivide`, `NumDecimal`, `NumEnter`, `Space`, `Escape`, `Tab`, `Backspace`,
`Enter`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Left`,
`Right`, `Up`, `Down`, and punctuation `Backquote`, `Minus`, `Equal`,
`BracketLeft`, `BracketRight`, `Backslash`, `Semicolon`, `Quote`, `Comma`,
`Period`, `Slash` (the symbols are also accepted). Sequences of up to four
strokes are joined by `", "` (`"Ctrl+K, Ctrl+C"`). Canonical text orders
modifiers Ctrl, Alt, Shift, Meta. `Ctrl` is Command on macOS, following Qt's
platform convention, so one keymap works on every desktop.

**Held keys** (held). One key or one modifier alone (`W`, `Shift`, `Space`),
optionally with modifiers that must also be down (`Ctrl+W`).

**Mouse gestures** (mouse). Optional modifiers and optional held key, then a
button and an action, or a wheel:

```text
[Ctrl+][Alt+][Shift+][Meta+][<Key>+]<Button><Action>     e.g. Alt+LeftDrag, Space+LeftDrag
[Ctrl+][Alt+][Shift+][Meta+]<Wheel>                       e.g. Ctrl+Wheel
```

| Part | Values |
| --- | --- |
| Button | `Left`, `Right`, `Middle`, `Back`, `Forward` |
| Action | `Click`, `DoubleClick`, `Drag`, `Press` (fires on press, without waiting for click or drag) |
| Wheel | `Wheel` (either direction, as an axis), `WheelUp`, `WheelDown`, `WheelLeft`, `WheelRight` (horizontal and trackpad) |
| Held key | any key name above except modifiers, e.g. `Space`, `Z` |

`Drag` denotes a dragging action; its start threshold belongs to the owning
input handler and is not stored in this format. A common four-logical-pixel
threshold is a planned convention. Current handlers use Qt's
`startDragDistance` or their own movement threshold; the format does not
promise that every gesture starts after exactly four pixels.

## 5. Built-in keymap and command catalogue

`cypher_default` binds Mason's commands Hammer-style where Hammer has a
convention. The tables are the plan of record for the command set; `-` means
listed and unbound. Commands appear in menus, toolbars, the command palette,
and the console from the same registration.

### 5.1 `global`

| Command | Label | Default |
| --- | --- | --- |
| `file.new` | New | `Ctrl+N` |
| `file.open` | Open... | `Ctrl+O` |
| `file.save` | Save | `Ctrl+S` |
| `file.save_as` | Save As... | `Ctrl+Shift+S` |
| `file.save_all` | Save All | `Ctrl+Alt+S` |
| `file.close` | Close | `Ctrl+F4` |
| `file.import` | Import... | - |
| `file.export` | Export... | - |
| `file.open_project` | Open Project... | - |
| `file.open_workspace` | Open Workspace... | - |
| `file.exit` | Exit | `Ctrl+Q` |
| `edit.undo` | Undo | `Ctrl+Z` |
| `edit.redo` | Redo | `Ctrl+Y`, `Ctrl+Shift+Z` |
| `edit.repeat_command` | Repeat the selected Command History entries, or the last repeatable command (Hammer) | `Shift+G` |
| `edit.cut` | Cut | `Ctrl+X` |
| `edit.copy` | Copy | `Ctrl+C` |
| `edit.paste` | Paste | `Ctrl+V` |
| `edit.paste_special` | Paste Special... | `Ctrl+Shift+V` |
| `edit.duplicate` | Duplicate | `Ctrl+D` |
| `edit.delete` | Delete | `Delete` (macOS also `Backspace`) |
| `edit.select_all` | Select All | `Ctrl+A` |
| `edit.select_none` | Select None | `Ctrl+Shift+A` |
| `edit.invert_selection` | Invert Selection | `Ctrl+Shift+I` |
| `edit.find` | Find... | `Ctrl+F` |
| `edit.find_replace` | Find and Replace... | `Ctrl+Shift+H` |
| `view.console` | Console | `Backquote` |
| `view.outliner` | Outliner | `Alt+1` |
| `view.properties` | Properties | `Alt+2` |
| `view.assets` | Asset Browser | `Alt+3` |
| `view.history` | History | `Alt+4` |
| `view.command_palette` | Command Palette | `Ctrl+Shift+P` |
| `view.fullscreen` | Full Screen | `F11` |
| `view.layout.reset` | Reset Layout | `Ctrl+Shift+R` |
| `view.layout.save` | Save Layout As... | - |
| `view.layout.manage` | Layouts... | - |
| `tools.settings` | Settings... | `Ctrl+Comma` |
| `tools.theme_editor` | Theme Editor | - |
| `tools.keymap_editor` | Keyboard and Mouse... | - |
| `tools.plugins` | Plugins... | - |
| `tools.reload_plugins` | Reload Plugins | - |
| `help.manual` | Manual | `F1` |
| `help.shortcuts` | Shortcut Reference | `Ctrl+Slash` |
| `help.about` | About Mason | - |

### 5.2 `map`

| Command | Label | Default |
| --- | --- | --- |
| `map.tool.select` | Selection Tool | `Shift+S` |
| `map.tool.camera` | Camera Tool | `Shift+C` |
| `map.tool.entity` | Entity Tool | `Shift+E` |
| `map.tool.block` | Block Tool | `Shift+B` |
| `map.tool.texture` | Texture Application | `Shift+A` |
| `map.tool.apply_material` | Apply Current Material | `Shift+T` |
| `map.tool.decal` | Decal Tool | `Shift+D` |
| `map.tool.overlay` | Overlay Tool | `Shift+O` |
| `map.tool.clip` | Clipping Tool | `Shift+X` |
| `map.tool.vertex` | Vertex Tool | `Shift+V` |
| `map.tool.path` | Path Tool | `Shift+P` |
| `map.tool.measure` | Measure Tool | `Shift+M` |
| `map.tool.terrain` | Terrain Tool | - |
| `map.tool.patch` | Patch Tool | - |
| `map.brush.hollow` | Hollow | `Ctrl+H` |
| `map.brush.carve` | Carve | `Ctrl+Shift+C` |
| `map.brush.merge` | Merge | `Ctrl+Shift+M` |
| `map.brush.to_mesh` | Convert selected brush objects to authored meshes | - (remappable; no verified Hammer default adopted) |
| `map.brush.snap_to_grid` | Snap Selection to Grid | `Ctrl+B` |
| `map.transform.dialog` | Transform... | `Ctrl+M` |
| `map.transform.flip_horizontal` | Flip Horizontally | `Ctrl+L` |
| `map.transform.flip_vertical` | Flip Vertically | `Ctrl+I` |
| `map.transform.rotate_cw` | Rotate 90 Clockwise | - |
| `map.transform.rotate_ccw` | Rotate 90 Counter-Clockwise | - |
| `map.entity.tie` | Tie to Entity | `Ctrl+T` |
| `map.entity.move_to_world` | Move to World | `Ctrl+W` |
| `map.entity.properties` | Object Properties | `Alt+Enter` |
| `map.entity.connections` | Entity Connections | `Ctrl+Shift+O` |
| `map.group.create` | Group | `Ctrl+G` |
| `map.group.ungroup` | Ungroup | `Ctrl+U` |
| `map.hide.selected` | Hide Selected | `H` |
| `map.hide.unselected` | Hide Unselected | `Shift+H` |
| `map.hide.show_all` | Show All | `U` |
| `map.visgroup.create` | New Visgroup from Selection | - |
| `map.layer.create` | New Layer | - |
| `map.layer.move_selection` | Move Selection to Layer... | - |
| `map.select.touching` | Select Touching | `Ctrl+Shift+T` |
| `map.select.inside` | Select Inside | - |
| `map.select.same_material` | Select Same Material | - |
| `map.select.same_class` | Select Same Class | - |
| `map.grid.smaller` | Smaller Grid | `BracketLeft` |
| `map.grid.larger` | Larger Grid | `BracketRight` |
| `map.grid.show` | Show Grid | `Shift+R` |
| `map.grid.snap` | Snap to Grid | `Shift+W` |
| `map.grid.show_3d` | Show 3D Grid | `Ctrl+Shift+W` |
| `map.texture.lock` | Texture Lock | `Shift+L` |
| `map.texture.scale_lock` | Texture Scale Lock | - |
| `map.texture.browser` | Material Browser | `Alt+T` |
| `map.texture.replace` | Replace Materials... | - |
| `map.view.center_selection_2d` | Center 2D Views on Selection | `Ctrl+E` |
| `map.view.center_selection_3d` | Center 3D View on Selection | `Ctrl+Shift+E` |
| `map.view.top` | Top View in the active pane (Hammer) | `F2`, `Ctrl+Alt+1` |
| `map.view.front` | Front View in the active pane (Hammer) | `F3`, `Ctrl+Alt+2` |
| `map.view.side` | Side View in the active pane (Hammer) | `F4`, `Ctrl+Alt+3` |
| `map.view.cycle_2d` | Cycle the active pane through Top, Front, Side (Hammer) | `Ctrl+Space`; `Meta+Space` (Control+Space) on macOS, where Command+Space is Spotlight |
| `map.view.perspective` | 3D View | `Ctrl+Alt+4` |
| `map.view.maximize` | Maximize Pane | `Shift+Z` |
| `map.render.wireframe` | Wireframe | - |
| `map.render.shaded` | Fixed directional-light material preview | - (viewport F6 below) |
| `map.render.fullbright` | Unlit material-color preview | - (viewport F5 below) |
| `map.render.normals` | Face normals diagnostic | - (viewport F7 below) |
| `map.render.mesh_edges` | Toggle surface face outlines | - (viewport F11 below) |
| `map.render.wire_overlay` | Toggle all wire edges over filled geometry | - (viewport F8 below) |
| `map.cordon.toggle` | Cordon | - |
| `map.cordon.edit` | Edit Cordon | - |
| `map.go_to` | Go To an object ID, entity name, or position (Hammer 4's go to brush) | `Ctrl+Shift+G` |
| `map.info` | Map Info: counts by kind and class, materials, extent | |
| `map.check` | Check for Problems | `Alt+P` |
| `map.compile` | Compile... | `Ctrl+F9` |
| `map.run` | Run Map | `F9` |
| `map.stop` | Stop Game | `Shift+F9` |
| `map.leak.show` | Show Leak | `Ctrl+Shift+L` |

### 5.3 Viewports and tools

| Context | Command | Label | Default |
| --- | --- | --- | --- |
| `map.viewport` | `map.tool.cancel` | Cancel | `Escape` |
| `map.viewport` | `map.tool.confirm` | Apply | `Enter`, keypad Enter |
| `map.viewport` | `map.view.cycle` | Focus next visible pane; Mason convenience | `Tab` |
| `map.viewport` | `map.render.fullbright` / `shaded` / `normals` | Active-pane CPU preview modes; Shaded does not evaluate authored lights | `F5` / `F6` / `F7` |
| `map.viewport` | `map.render.wire_overlay` / `mesh_edges` | Active-pane edge overlays; F11 overrides window fullscreen in geometric views | `F8` / `F11` |
| `map.selection.objects` / `map.selection.groups` | `map.mesh.flip_normals` | Reverse all faces of selected authored mesh objects | `F` |
| `map.viewport.2d` | `map.nudge.left` / `right` / `up` / `down` | Nudge by Grid | arrows |
| `map.viewport.2d` | `map.nudge_fine.left` / `right` / `up` / `down` | Nudge by 1 Unit | `Shift`+arrows |
| `map.viewport.3d` | `map.camera.mouselook` | Toggle Mouselook | - (reserved; command not registered) |
| `map.tool.clip` | `map.clip.cycle_mode` | Keep Both / Front / Back | `Shift+X` |
| `map.tool.clip` | `map.clip.flip` | Flip Clip Plane | - |
| `map.tool.vertex` | `map.vertex.merge` | Merge Vertices | `Ctrl+M` |
| `map.tool.vertex` | `map.vertex.split_edge` | Split Edge | - |
| `map.tool.vertex` | `map.vertex.snap` | Snap Vertices to Grid | `Ctrl+B` |
| `map.tool.texture` | `map.texture.fit` | Fit | - |
| `map.tool.texture` | `map.texture.align_world` | Align to World | - |
| `map.tool.texture` | `map.texture.align_face` | Align to Face | - |
| `map.tool.texture` | `map.texture.justify_left` / `right` / `top` / `bottom` / `center` | Justify | - |
| `map.tool.block` | `map.block.next_shape` | Next Shape | - |
| `map.tool.entity` | `map.entity.next_class` | Next Class | - |

### 5.4 Held actions (`map.viewport.3d`)

The whole-mesh `F` operation is a Mason adaptation of Hammer's normal reversal,
not a claim of component selection support. Face-mode normal reversal and
edge-mode bevel remain unavailable until their persistent selection adapters
are connected. `map.mesh.triangulate` is available through the Mesh menu,
Tool Properties and command palette without a default shortcut.

| Action | Default |
| --- | --- |
| `map.camera.forward` | `W`, `Up` |
| `map.camera.back` | `S`, `Down` |
| `map.camera.left` | `A`, `Left` |
| `map.camera.right` | `D`, `Right` |
| `map.camera.up` | `E`, `PageUp` |
| `map.camera.down` | `Q`, `PageDown` |
| `map.camera.fast` | `Shift` |
| `map.camera.slow` | `Alt` |

Movement keys fly while the 3D view owns a captured Look drag (RMB by default)
or the Camera tool is active without another camera drag. Orbit, Pan and
Dolly drags suppress held flight movement. A persistent mouselook toggle is
not implemented. Grid and fine nudge commands apply to 2D views only; 3D arrow
keys are camera-navigation alternatives, not geometry nudges.

### 5.5 Mouse actions

This table is the authored declaration catalogue. The four 3D camera
actions and wheel flight-speed actions currently resolve configurable mouse gestures; geometry selection,
transformation and 2D pan/zoom/context menus still use fixed mouse handlers.
For example, the 2D pan declaration includes `RightDrag`, but the current
orthographic handler pans with MMB or Space+LMB. Editing that declaration does
not add right-drag pan. Settings labels those entries **Not remappable yet**.
Supported camera declarations use button drags with modifiers and optionally
Space; Dolly also supports vertical wheel input. Flight-speed mouse actions
support vertical wheel input with modifiers. During a captured Look gesture,
`map.camera.look` mouse declarations take priority over ordinary tool/selection/
viewport/map/global contexts. An absent match falls through to those contexts;
an explicit unbinding or unknown reservation consumes the wheel without dolly.
Wheel speed follows the resolved action, so directions can be reversed in a
profile. Each full notch doubles/halves base speed; fractional trackpad deltas
scale smoothly. Multiple notches publish one atomic settings change, including
at descriptor limits. Zoom wheel inversion does not invert speed actions.
Captured Look and held flight keys survive handled wheel input, including a
failed settings write. Camera clicks, other held keys and horizontal wheel
declarations are retained but not routed.

| Context | Action | Default |
| --- | --- | --- |
| `map.viewport.3d` | `map.camera.look` | `RightDrag`, `Alt+MiddleDrag` |
| `map.viewport.3d` | `map.camera.orbit` | `Alt+LeftDrag` |
| `map.viewport.3d` | `map.camera.pan` | `MiddleDrag`, `Space+LeftDrag` |
| `map.viewport.3d` | `map.camera.dolly` | `Alt+RightDrag`, `Wheel` |
| `map.camera.look` | `map.camera.speed_increase` | `WheelUp` |
| `map.camera.look` | `map.camera.speed_decrease` | `WheelDown` |
| `map.viewport.2d` | `map.view.pan` | `RightDrag`, `MiddleDrag`, `Space+LeftDrag` |
| `map.viewport.2d` | `map.view.zoom` | `Wheel` |
| `map.viewport` | `map.select.pick` | `LeftClick` |
| `map.viewport` | `map.select.toggle` | `Ctrl+LeftClick` |
| `map.viewport` | `map.select.box` | `LeftDrag` (on empty space) |
| `map.viewport` | `map.select.cycle` | `Alt+LeftClick` (next object under the cursor) |
| `map.viewport` | `map.transform.drag` | `LeftDrag` (on the selection) |
| `map.viewport` | `map.transform.clone_drag` | `Shift+LeftDrag` (on the selection) |
| `map.viewport.2d` | `map.context_menu` | `RightClick` |

### 5.6 Panels

| Context | Command | Default |
| --- | --- | --- |
| `console` | `console.history_previous` | `Up` |
| `console` | `console.history_next` | `Down` |
| `console` | `console.complete` | `Tab` |
| `console` | `console.clear` | `Ctrl+L` |
| `outliner` | `outliner.rename` | `F2` |
| `outliner` | `outliner.focus_in_views` | `F` |
| `properties` | `properties.apply` | `Enter` |
| `properties` | `properties.revert` | `Escape` |

## 6. Limits

4 triggers per command or action, 4 strokes per chord, 2,048 contexts, 8
keymap levels, 16 MiB per file.

## 7. Versions

V1 (2026-09-25) had `id`, `name`, `base`, and `bindings`. V2 adds `author`,
`description`, `held`, `mouse`, `platforms`, and the context stack; it reads
V1 unchanged and saving writes V2.
