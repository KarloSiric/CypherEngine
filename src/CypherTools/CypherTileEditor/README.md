# CypherTileEditor

CypherTileEditor is CypherEngine's first blockout map authoring workspace. It is a small Qt 6 desktop tool for drawing a playable floor plan on a grid, assigning blockout materials, placing gameplay markers and doors, validating the result, and generating renderer-neutral floor, boundary-wall, and door boxes.

The tool is deliberately split into two layers:

- `Core/` owns the document, edit transactions, undo/redo history, validation, deterministic `.cymap` persistence, and geometry generation. It has no Qt dependency and is intended to be shared by a future Mason map workspace and an in-game ImGui front end.
- `Gui/` owns the standalone Qt presentation: canvas, embedded CypherRender viewport, docks, material and room-stamp palettes, inspector, console, settings, and configurable shortcuts.

The authored map remains the source of truth. Generated geometry is derived data:

```text
.cymap source
    -> validation
    -> floor, exposed-boundary, and door box generation
    -> future map cooker / CypherWorld ingestion
    -> CypherRender draw submission
```

CypherRender does not parse editor documents. This boundary allows the editor and runtime renderer to evolve independently while both consume the same explicit map build result.

The source-level editor study and implementation program for extending this tool
without turning its cell document into Mason's future scene graph are documented
in [TrenchBroom Editor Systems Research](../../../docs/trenchbroom_editor_systems_research.md).

## Build and run

The development build requires Qt 6.5 or newer with the Core, Gui, Widgets,
OpenGLWidgets, and Svg components. CMake finds an installed Qt SDK through its
normal package search; set `Qt6_DIR` or `CMAKE_PREFIX_PATH` when Qt is not
installed in a standard location. The current macOS `.app` is a local
development bundle and uses the installed Qt frameworks.

From the repository root:

```sh
cmake --preset tile-editor-debug
cmake --build --preset tile-editor-debug --target CypherTileEditor
./out/build/tile-editor-debug/bin/CypherTileEditor.app/Contents/MacOS/CypherTileEditor
```

On platforms where CMake does not create an application bundle, run:

```sh
./out/build/tile-editor-debug/bin/CypherTileEditor
```

Run the focused verification suite with:

```sh
ctest --preset tile-editor-debug -R 'cypher_tile_'
```

Open the included renderer-ready example directly:

```sh
./out/build/tile-editor-debug/bin/CypherTileEditor.app/Contents/MacOS/CypherTileEditor \
    ./assets/maps/tile_editor_demo.cymap
```

The **3D Map** panel is an in-process WYSIWYG view of the current map driven by the
real CypherRender API. It rebuilds from the in-memory document during paint
strokes (at most every 40 ms) and immediately after committed edits, so it does
not require a save. The default workspace places **3D Map at top-left**, **Top XY
at top-right**, **Front XZ at bottom-left**, and **Side YZ at bottom-right**.
Active-pane header highlighting and the colored outline are both off by default.
Configure them independently in **Settings > Appearance**; **View > Show Active
View Border** also toggles the outline. Its color remains configurable. Front
and Side are orthographic QPainter
projections of the same generated boxes; 3D Map uses CypherRender. Selection
from Top/Front/Side or a left-click on 3D geometry highlights the matching cells
across all four views. The 3D picker selects the nearest visible generated box.
Use **Ctrl+1/2/3/5** to focus Top/3D/Front/Side, **Ctrl+4** to restore four
views, **Shift+Space** to maximize/restore the active pane, and **Ctrl+Shift+F**
to frame all panes. A compact title overlay in the top-left of every viewport
keeps its chooser, frame/maximize, options, and close controls available without
reserving a full-width header row. Double-click the overlay to maximize it. Each
overlay displays its physical pane number. One, two, three, or four panes can remain
visible; closing a pane hides it without destroying its camera or pan state, and
**Restore Four Views** brings every pane back. Right-click a pane header, or
right-click and release without dragging inside any viewport, to open the same
view-type, framing, maximize, visibility, and restore commands. A right-button
drag keeps its existing navigation behavior. Click a pane's view title to choose
Top, 3D, Front, or Side. Multiple panes may independently show the same 2D projection, each
with its own pan and zoom while sharing the map, tools, and exact selection.
Choosing 3D relocates the existing renderer pane rather than creating another
renderer; its camera and graphics context are preserved. View choices, pane
order, visibility, and splitter sizes persist between sessions. Reset Workspace
Layout restores the default four-view arrangement. The views form one continuous
editor surface separated only by six-pixel dark, subtly beveled gutters; their
thickness can be changed from 3–16 pixels under **Settings > Workspace**.
Viewport input follows the pointer by default: entering or moving over a pane
focuses it immediately, so Space-pan, camera movement, tool keys, and view
shortcuts work without a preparatory click. Active mouse drags, popup menus,
and modal dialogs keep ownership until they finish. This behavior can be
disabled under **Settings > Workspace**.
In the 3D Map panel, hold **right mouse** to look and fly with **WASD**;
**Q/E** move down/up along world Z. Once right-mouse fly navigation is engaged,
**Shift** moves four times faster and **Ctrl** moves at one-quarter speed by
default; both multipliers are configurable. Diagonal input cannot increase speed.
Release right mouse or press **Escape** to stop navigation and restore the pointer.
Losing focus, hiding the pane, or changing documents also releases navigation.
**Alt+right mouse** temporarily orbits and **middle mouse** pans. Shift has one
meaning during right-mouse flight regardless of key order: it accelerates movement.
The wheel changes fly speed in Fly mode and camera distance in Orbit mode;
high-resolution trackpad scrolling is supported. Fly/Orbit switching is available
from the Camera menu and can be bound under **Settings > Shortcuts**. It is
unbound by default so Tab retains normal focus traversal. **F** frames selected-region geometry,
or the whole map without a selection. The pane's frame button and **Frame All
Views** always frame the map. **View > Camera at Player Spawn** moves the camera
to a flat-floor spawn; it is free flight and has no player collision or gravity.
The checkable **Camera > Auto Orbit** command starts automatic orbiting and can
receive a custom shortcut; it is off and unbound by default. Startup maximizes the application and
frames the map by default; both behaviors are configurable in Settings.
Framing uses occupied geometry rather than the document's unused grid area.
The **Camera** button in the 3D pane header and **View > Camera** expose Fly/Orbit
mode, map and selection framing, player-spawn framing, exact fly speed, faster
and slower actions, navigation-hint visibility, and a direct Camera Settings
link. **View Direction** supplies exact Perspective, Top, Bottom, Front, Back,
Left, and Right views plus a level-pitch command. Camera up/down moves by the
open map's configured level height. Direction presets use Orbit for immediate
inspection without overwriting the user's saved Fly/Orbit navigation preference.
**Saved Views** stores four session bookmarks;
each recalls position, direction, and orbit distance while retaining current
camera settings and renderer bounds. Replacing the map clears those bookmarks.
Navigation hints are off by default. Fly-mode and speed changes update the menu
and persist to editor preferences. Camera and panel commands can receive custom
shortcuts in Settings; the preset and bookmark actions start unbound.
The 3D camera centers the projected bounds with padding; empty orthographic
views start at a readable, configurable pixels-per-cell scale.

The icon-only left rail provides Select (V), Paint (B), Erase (E), Rectangle
(R), Spawn (P), Door (D), Pan (H), Pick Material and Dimensions (I), Line (L),
and Fill (G). Fill follows connected cells with the same material, floor level,
and wall height; an empty region fills only empty cells. Line/fill each form
one undo entry. Double-click a room footprint in the pieces palette to stamp
it around the selected cell. Palette swatches show the actual blockout colors;
these remain available in **Blockout Palette**. **Project Materials** displays actual cooked texture thumbnails.

The compact workspace keeps a slim icon rail on the left. On the right,
**Materials & Pieces** and **All Objects** share the upper dock tabs, with
**Properties** below. Materials & Pieces contains **Project Materials / Blockout
Palette / Room Pieces** tabs. **Console** is a separate bottom dock with two
tabs: **Editor Commands** controls the current map/editor session, while **Local
Shell** runs one asynchronous zsh/bash command at a time with an editable working
directory, history, streamed stdout/stderr, exit status, stop, restart, clear,
and copy controls. The shell tab is deliberately a noninteractive command runner;
programs that require a PTY are outside its contract. Type `shell` in Editor
Commands or use **View > Open Local Shell Runner** to switch to it. The dock is
hidden by default. Properties contains Selection, Paint, Map, and Validation
tabs. Filter the object tree by type, coordinate, level, or numeric material
slot. It supports multiple selected rows, synchronized with the owning cells in
every pane; a floor, door, and spawn at the same cell represent one selected cell.
Double-click an object to frame it in 3D. The tree shows at most 1,024 matching
rows; filtering searches the whole document.
**View > Reset Workspace Layout** restores this arrangement. Existing window
geometry is preserved while the old dock arrangement migrates once.
The top toolbar has icon toggles for the console, assets/pieces, object tree,
properties, and left tool rail. A checked icon indicates an open panel; hover
an icon for its label. Project Materials is initially selected in the assets dock.
The tiled-library icon controls the **Materials & Pieces panel**; the separate
diagonal-texture icon controls material rendering inside the 2D views. Both use
the configured accent color while checked, so panel visibility and viewport
shading cannot be mistaken for the same command. Validate, Build Geometry, and
Runtime Preview use blue, gold, and green command glyphs respectively; Stop
Preview uses a red square.
These toggles and the configuration open/reload actions can also receive custom
keys in Settings > Shortcuts; new actions are unbound until assigned.

The searchable Room Pieces palette provides 24 presets: rooms, straight
corridors, four orientations each of corner and T junction footprints, a cross
junction, four stair directions, four door sides, and two boundary wall heights.
Single-click a stair, door, or wall preset to prepare its tool and orientation.
Double-click a room or corridor footprint to stamp it around the selected cell.
Stamps preserve the current material and elevation, reject out-of-bounds
placement before editing, and undo as one operation. Wall corners follow the
generated boundary of the footprint; these are not standalone brush meshes.

Use **Select (V)** and left-drag in Top to select an inclusive rectangle, including
empty placement cells; click an unselected cell to replace the selection with
that cell. **Shift** adds cells, **Ctrl** toggles them, and **Alt** subtracts them,
with either clicks or marquees. Here and in configurable Qt shortcuts, **Ctrl**
is **Command** on macOS. Front and Side also support geometry clicks and
marquees; modified 3D clicks select the nearest hit cell. All views share the
exact selected cells, so holes in a selection remain unselected. A dashed
bounding rectangle provides a framing guide without adding cells to the set.

Clicking an already selected cell preserves the set. In Top, drag an already
selected occupied cell without modifiers to move the selection: translated
outlines preview the snapped destination, and releasing commits one move.
**Escape** or focus loss cancels the pending drag without changing the map.
**Escape** otherwise clears a committed selection. Copying uses Duplicate or
the explicit offset controls rather than a drag modifier.

All orthographic views support **middle-drag**, **right-drag**,
**Space + left-drag**, or **Pan (H) + left-drag**, with wheel zoom anchored beneath
the pointer. A stationary right-click opens that viewport's options instead of
panning it. Each pane keeps its own pan and zoom. Top's **F** frames the selection,
falling back to the whole map. The pane header frame buttons always frame the map.

While a 2D viewport has focus, or the 3D viewport has focus without camera navigation:

- **Arrow keys** move the exact selected cells one cell along map X/Y.
- **Ctrl+D** duplicates one bounding-selection width to the right.
- **Ctrl+R** rotates clockwise around the bounding rectangle's upper-left cell,
  rotating stair directions, door sides, and spawn yaw with the selected cells.
- **Page Up / Page Down** raise or lower the selected floors one level.
- **Shift+Page Up / Shift+Page Down** increase or decrease selected wall heights
  one level.
- **Delete** removes selected cells and their markers.
- **Ctrl+A** selects authored floor and marker cells, excluding the unused grid.
- **Ctrl+Shift+A** clears the selection.

The Selection Transform controls also accept explicit X/Y offsets for moving or
copying. Every command is one undoable transaction. Moves preserve marker IDs;
duplicates create new door IDs and leave the unique player spawn at its original
location. Occupied destinations and out-of-bounds moves are rejected without
partial edits. Unselected cells inside a sparse selection's bounding rectangle
remain untouched. These shortcuts do not consume typing in the console, search
fields, or property controls. In Front and
Side, arrow movement still uses **map X/Y**, not projected screen directions.

The Selection inspector supports bulk editing of floor presence, elevation,
wall height, material slot, shape, and stair-step count. Mixed values are marked
as mixed. Committing one field changes only that property across the selection,
preserving each cell's other values; focusing or leaving an unchanged field does
not create an edit. Each committed bulk change is one undo operation. Enabling
floors fills selected empty cells without replacing existing floor properties;
disabling floors removes selected floors and their markers. Spawn yaw remains an
individual-marker property.

Front and Side can author on an explicit construction layer. **Properties >
Paint > Front view row Y** fixes the row for Front; **Side view column X** fixes
the column for Side. Selecting a cell updates these controls. The pointer's
horizontal position chooses the cell along that layer; its vertical position
chooses the floor level at the start of a paint stroke. That level stays fixed
throughout the stroke. Paint and Erase support continuous strokes; Rectangle
and Line create one-cell-wide runs along the fixed row or column. Spawn, Door,
and the eyedropper also target that construction layer. Fill remains a Top tool.
While an authoring tool is active, the pane always shows its fixed hidden-axis
slice and stroke floor level, even when viewport metrics are disabled. A dashed
cell ghost previews the exact fixed-slice target under the pointer. The slice is
colored for the hidden axis: green Y in Front and red X in Side. Attempting Fill
in Front or Side explains that hidden-axis connectivity must be chosen in Top.
The document still stores one floor per cell: changing its elevation replaces
that cell's level rather than creating stacked floors.

The 3D pane supports single-click Paint, Erase, Spawn, Door, and eyedropper
operations. A geometry hit targets its owning cell; Paint can also create a
cell where the view ray meets the current brush floor plane inside map bounds.
Erase, markers, and the eyedropper require a geometry hit. Each edit click is
one transaction; drag drawing remains in the orthographic views. Modified left
clicks select cells even with an authoring tool active. These controls edit the
tile document, not arbitrary 3D faces, vertices, or separate wall objects.

The charcoal theme with orange selected tools is in `Resources/editor.qss`,
bundled as a Qt resource. Tool SVGs
are bundled from Tabler Icons with the MIT license and pinned provenance in
`Resources/Icons/tabler/README.md`. No network access is needed at runtime.
Grid visibility, marker visibility, grid density, colors, startup behavior,
viewport activation, orthographic wireframe/fill display, default document dimensions, and shortcuts are editable
in Settings. **Auto** beside the toolbar's grid spacing uses that spacing as a
minimum: zooming out increases the visible interval in powers of two, and
zooming in reveals finer lines. Settings controls the minimum screen spacing
(4–64 pixels, default 12). Disable Auto for fixed spacing. Major grid lines
remain anchored to authored coordinates; enable viewport metrics to show the
current grid interval, coordinates, scale, and selection information.
Coordinate rulers are a separate display option. When enabled, Top shows world
X/Y numbers and Front/Side show X/Z or Y/Z numbers in themed top and left bands.
Their label interval coarsens automatically as the view zooms out, remains
anchored to authored world zero, and does not alter grid snapping or map metrics.
New preferences use **Radiant Dark**: charcoal panels, dark blue orthographic
backgrounds and orange selected tools. The optional outline color defaults to
red and can be changed independently. **Settings >
Appearance** offers eleven palettes: **Radiant Dark**, **Slate**, **Hammer
Charcoal**, **Radiant Light**, **Midnight**, **Warm Workshop**, **Blueprint
Blue**, **Graphite**, **High Contrast Dark**, **Coastal Dusk**, and **Sandstone
Light**. The collection covers neutral, warm, blue, high-contrast, and light
workspaces, with individual UI, grid, axis, floor, wall, stair, and door colors
remaining editable. Applying a preset replaces colors only; navigation,
display toggles, document defaults, and shortcuts remain unchanged. Default
text is 11 pt with 24 px toolbar icons; both sizes are configurable.
Orthographic views draw the grid behind geometry, distinguish wall thickness
and stair direction, and optionally dim distant geometry in Front and Side.
Depth cues improve overlapping wireframes; they are not hidden-line removal.
Disable internal tile seams in Canvas & Grid for cleaner Top wireframes while
retaining material, elevation, and shape boundaries.
Grid display spacing does not resample the authored map or change cell size.

Colored coordinate axes and hover activation are enabled by default; viewport
metrics, material identification, camera hints, and active-pane highlighting
remain independent display choices. **Canvas & Grid > Show colored coordinate
axes and 3D orientation triad**, **View > Show XYZ Coordinate Axes**, or
**Ctrl+Shift+X** controls the common axis language. X is red, Y
is green, and Z is blue in Top, Front, Side, and the camera-oriented 3D triad.
**Center coordinate axes on the map** defaults on. Centering places Top's guides
at the document center and Front/Side's vertical guide at the horizontal
document center. Front/Side keep their horizontal reference at world Z = 0.
Disable centering to use the authored origin instead. Appearance controls the
exact X/Y/Z colors while retaining their semantic hues. These guides never move
geometry, change cell coordinates, or change editing snap.

**Show Materials in 2D Views** is enabled by default. The material icon in the
main toolbar and the matching View-menu command toggle material surfaces in
Top, Front, and Side. Bound slots show their cooked `.cymat` base-color texture,
tint, and UV scale. Unbound slots use their named blockout palette colors;
unavailable bound materials show a magenta checker rather than an unrelated
palette color. Wall, stair, door, and selection outlines remain visible.
Door markers retain their diagnostic orange appearance and are identified as
markers without an authored surface material, matching the 3D renderer.
Turning material surfaces off restores the configured wireframe/solid mode.

Enable **View > Show Material Identification** to identify a hovered or selected
cell's material slot, name, and path in the pane. This text is off by default.
Its compact bottom label works independently of view metrics; a hover tooltip
exposes the full path and any missing-asset diagnostic. **Settings > Canvas
& Grid** also controls material visibility, identification, and surface opacity
(20–100%, default 80%). These preferences are saved in the INI `Viewport`
section and exported editor profiles. Display changes do not modify the map.

The orthographic previews are flat and unlit: Top displays the material over
each tile footprint, including stair footprints; Front and Side display it over
the projected generated boxes. The 3D pane remains the lit CypherRender view.
Rebinding, undo/redo, and material cooking in Project Materials update the panes
without reopening the map. Use the material browser's **Refresh** after external
cooking. A shared cache limits thumbnails to 256 pixels per axis, shares duplicate
material images, and performs no asset reads during painting or navigation.

**Edit > Open Editor Configuration** opens the readable `editor.ini` in the
platform's application configuration directory. On macOS the standard profile
uses `~/Library/Preferences/CypherEngine/CypherTileEditor/editor.ini`. The
Settings dialog displays the resolved path. Its Appearance, Viewport, Grid,
Camera, Workspace, Map, and Shortcuts sections override the corresponding
native preferences; omitted keys retain their current values. Save the file
and choose **Edit > Reload Editor Configuration** to apply it without restarting
or reloading the map. The Settings dialog and toolbar grid options write the
same configuration. Invalid colors, unsupported versions, malformed values,
and duplicate shortcuts reject the entire reload and report the problem in
the console. Numeric settings are clamped to their supported ranges. Writes
replace the file atomically, so a failed save preserves the previous file.

**Settings > Apply** activates and saves the current settings while keeping the
dialog open. **OK** applies and closes. **Cancel** discards only edits made since
the last Apply; already-applied settings remain active. Duplicate shortcuts
disable both Apply and OK until resolved. **Restore Defaults** stages all editor
preference defaults for inspection; Apply or OK commits them. Color presets
continue to replace colors only. The Settings dialog is modal: close it to test
camera input after applying navigation changes.

Use **Edit > Configuration Profiles > Import Configuration Profile…** to load a
saved editor setup, or **Export Configuration Profile…** to save the active
preferences to another INI file. Import validates a candidate before replacing
the active `editor.ini` and applying it; invalid profiles preserve the current
setup. Omitted settings retain their current values. An imported profile is
copied into the active configuration, rather than becoming a linked file that
is watched for changes. Export is a snapshot of the applied preferences.
The console provides the same operations:

```text
config_export "/absolute/path/editor-navigation.ini"
config_import "/absolute/path/editor-navigation.ini"
```

Profiles contain appearance, viewport display, grid behavior, navigation,
workspace behavior, new-map defaults, and shortcuts. They do not contain the
map, edit history, selection, camera inspection pose, dock placement, pane order,
or window geometry. Importing a color or navigation configuration therefore
preserves the current editing workspace and authored map.

**Settings > Camera** persists the following navigation controls:

| Setting | Range / behavior |
| --- | --- |
| Movement speed | 0.1–1000 world units/second |
| Look sensitivity | 0.01–2 degrees/pixel |
| Pan sensitivity | 0.1–5 times normal middle-mouse pan |
| Wheel sensitivity | 0.1–5 times fly-speed adjustment or orbit-distance adjustment |
| Shift speed multiplier | 1–20 times movement speed; default 4 |
| Ctrl speed multiplier | 0.01–1 times movement speed; default 0.25 |
| Vertical field of view | 30–100 degrees |
| Invert mouse Y / wheel | Independent look and wheel direction switches |
| Fly navigation | Right dragging flies when enabled and orbits when disabled |
| Camera hints | Off by default; toggles control help independently of metrics; renderer errors remain visible |

Navigation settings also configure newly launched runtime previews; changing
them does not reconfigure a preview process that is already running. Camera math is shared
by the Qt and runtime adapters in `Core/CypherTileCamera.*`; input focus and mouse
capture belong to their respective frontends. Geometry refreshes preserve a
camera that the user has navigated; framing explicitly changes its position.

Use **Tools > Launch Runtime Preview**, press **F6**, or enter `preview` in the
command console to launch the separate game-window preview. The editor writes a
temporary snapshot without changing the document's path, saved revision, or
dirty state. Committed edits are debounced into that snapshot and the runtime
window reloads them automatically. Building `CypherTileEditor` also builds this
companion and stages its cooked CYSH shader. In the runtime window, the same
fly/orbit, movement, pan, and wheel controls apply. **Tab** switches fly/orbit,
**F** frames the map, **G** goes to the player spawn, **R** reloads, and **Space**
toggles automatic orbiting. Escape releases navigation first, then exits.
Reloading the snapshot preserves the camera's exact inspection pose.

The command console accepts history navigation and full-line completion. Press
**Tab** and **Shift+Tab** to move through matches, **Ctrl+Space** to request
suggestions, and **Enter** to execute a command. Useful commands include
`layout2d`, `map3d`, `material list`, `material <slot>`, `door <side>`,
`validate`, `build`, `preview`, `preview_stop`, `camera_spawn`, `frame_selection`,
`camera_view top`, `camera_level up`, `camera_bookmark store 1`,
`camera_auto_orbit on`, and `shape stairs_east 8` (also flat,
stairs_north/south/west).

## Editing model

**Tools > Map Properties** (or **Properties > Map**) edits the currently open
document's width, height, cell size, and level height. Stage the values and click
**Apply** to update the views together as one undoable action. **Revert** discards
only staged field values. The console exposes the same operation:

```text
map_properties
map_properties 160 144 5 3
```

The first form opens the panel and reports current values; the second sets
width, height, cell size, and level height. Dimensions remain anchored at grid
origin `(0, 0)` and preserve existing cell coordinates, map/marker identities,
and material bindings. Shrinking is rejected if it would discard any authored
cell or marker; move or delete that content first. Cell size changes horizontal
world scale, while level height changes authored floor elevations, stair rises,
and wall heights. A camera that you have navigated stays in place; use Frame All
Views to refit after a large scale change.

The document remains open with its existing file path and edit history. Save
writes its current dimensions and metrics into `.cymap`. The embedded renderer
updates from memory, and an already-running runtime preview automatically
reloads the edited snapshot without a manual reopen or process restart. Undo
and redo also synchronize the views and runtime preview. These edits use the
existing CYKV map fields and require no map schema version change.

An active cell describes one unit of playable floor. Its authored properties
include floor level, wall height, and a stable blockout-material slot. Geometry
generation emits one floor slab per active cell and boundary wall boxes only
where an active cell touches an empty cell, a lower cell, or the document edge.
Shared edges between equal-height adjacent cells do not produce internal walls.
Current move/copy tools operate on cells and their owned markers. A generated
wall is selected through its owning cell, so moving a cell also regenerates its
walls. Independently authored or movable wall objects are not supported yet.
The next geometry step is a stable-ID rectangular wall primitive plus explicit
suppression of the original generated edge when detaching it. That extension
must carry object selection, undo, and a backward-compatible map schema update
together. Shape-library growth should use parameter presets and reusable
assemblies over a small primitive set, rather than one hard-coded tool per shape.
Unbound slots use the color-based graybox palette. Bound slots resolve project `.cymat` assets through the shared material-preview adapter in both the Qt viewport and standalone CypherRender runtime.

Choose a **Shape** in Paint Properties or the selected-cell Inspector to paint
flat floors or stairs rising north/east/south/west. A stair tile rises exactly one
map level across its footprint, using 2–32 solid steps (default 8). The direction
arrow points uphill. Shape and tread count work with paint, rectangles, lines,
fill, material picking, save/load, and grouped undo. Stair edges are open;
connect flat landings at the base floor level and at the next level. The matching
upper landing opening is generated without a blocking boundary wall.

Open `assets/maps/tile_editor_stairs.cymap` for two landings joined by a wide
staircase: 4-unit cells, a 2-unit rise, and eight steps per stair tile. Use the
spawn camera to inspect the stairs at eye height. Stairs currently have straight,
cardinal footprints; curved stairs, railings and arbitrary brush geometry remain
outside this milestone.

Player spawns and doors remain stable-ID markers rather than special floor
cells. A door is attached to one cardinal, exposed edge and replaces that wall
segment in generated geometry. Keeping special-object metadata separate from
cells prevents a mesh rebuild from silently changing entity identity or
behavior. Place spawns and doors on flat landings. A spawn on stairs reports a
gameplay warning; a door on stairs is an unsupported placement that blocks build.

Drag edits and room stamps are grouped into transactions. One drag or one stamp therefore creates one undo entry. Saving records the current revision; later edits make the document dirty until it is saved again or undone back to the saved revision.

## Source format

`.cymap` is a deterministic, human-readable CYKV document. The current writer
uses map schema **3**, adding an optional material binding table to schema 2's
`shape` and `stair_steps` fields. The reader accepts schemas **1, 2, and 3**.
Older editors cannot load maps saved by this version. Empty grid cells
are omitted, active cells are emitted in stable row-major order, and markers are
emitted in stable identity order. Loading validates the complete source into
temporary storage before replacing the live document, so a malformed file
cannot partially overwrite an open map.

Editor preferences and shortcuts are editable in `editor.ini` and mirrored to
native `QSettings`. Dock placement, pane order, window geometry, and recent files
remain in native settings. None enter `.cymap`, keeping project content
deterministic across users.

Validation distinguishes structural geometry errors from authoring warnings.
The live summary stays visible above the Inspector tabs. Counts and diagnostics
update during painting, cancellation, undo and redo. Double-click a diagnostic
to locate its cell; F7 opens the full results. The supported checks cover the
document structure, spawn and door placement; they do not check connectivity,
collision, or full gameplay reachability. A failed geometry build clears the
displayed mesh so stale geometry cannot be mistaken for the current map.
For example, a missing player spawn is still reported but does not prevent a
geometry build or runtime preview. Invalid door ownership, placement, or identity is
structural and must be corrected before export.

## Project materials and textures

Open `assets/maps/tile_editor_materials.cymap` to inspect brick, grid, and hazard
materials across the floor, boundary walls, and stairs. These are original test
textures included with the project. CMake builds the resource compiler and cooks
these assets alongside the editor; the browser seeds an editable per-project
cache from those staged outputs.

1. Choose the asset source directory with **Asset Root…**. Paths in the map are
   relative to that directory, for example `materials/dev/grid.cymat`.
2. Pick a numeric **Map slot**, select a recipe in **Project Materials**, and
   choose **Use Material** or double-click the recipe. If its cooked material is
   missing or unreadable, the editor prepares it and its declared texture/shader
   dependencies asynchronously with `CypherResourceCompiler`, then binds it on
   success. No separate manual cook is needed to start using a material.
3. The bound slot becomes the active paint material and the editor switches to
   Paint. Binding is undoable and changes every cell already using that slot.
   **Reset Slot** removes the binding, restoring its blockout color without
   changing cells' numeric slot assignments. Failed preparation keeps the
   previous binding and reports the compiler error in the console.
4. Use **Apply to Selection** to change only the material slot of the exact
   selected floor cells in one undoable action. This preserves elevation, wall
   height, shape, tread count, and unselected holes.
5. Save the `.cymap`; its slot-to-resource bindings travel with the map. Launch
   **Runtime Preview (F6)** to inspect the same textures with the engine renderer.

A recipe currently uses `shaders/tile_surface.cyshader`, one `base_color`
`.cytex` binding, optional `tint` vec4, and optional `uv_scale` vec2. UV scales
must be finite and nonzero; negative values mirror the texture. The renderer
supports RGBA8 color data, sRGB decoding, repeating linear sampling, and optional
GPU-generated mipmaps. Cube-face UVs currently span each generated box: there is
no face UV editor or world-space texel-density policy yet. Alpha, normal maps,
PBR lighting, cubemaps, and arbitrary shader contracts are outside this first
material path. Door markers retain their diagnostic orange appearance.

The browser's cooked cache is separate from source assets. Use **Update
Material** after changing a recipe, texture, or shader source: **Use Material**
reuses a readable cached result and does not check source freshness. **Refresh**
rescans recipes and reloads the embedded view, including externally cooked
changes. Successful material preparation updates the embedded view;
the running preview checks bound cooked dependencies every half second and
reloads them without resetting its camera. **R** forces a runtime retry. Changing
asset roots requires relaunching the runtime to switch projects. Invalid cooked
assets preserve the previous working materials and report an error; further map
edits do not dismiss the embedded warning. Repair and cook/reload to recover.

The reusable renderer texture API owns generational handles independently of Qt.
The shared adapter bounds decoded textures to 64 MiB each and 512 MiB per map
material set. It publishes replacement textures only after all bindings load.
It currently uploads mip zero and regenerates mipmaps on the GPU when requested;
explicit upload of all compiler-generated mip levels remains future work.

## Scope

This editor is for fast graybox maps and renderer/gameplay tests. It establishes the reusable document and command foundation for Mason, but it does not attempt to replace Mason's eventual arbitrary brush, mesh, entity-I/O, lighting, navigation, and world-partition workspaces.

The [editor configuration and navigation reference research](../../../docs/tile_editor_reference_research.md)
compares relevant NetRadiant, Hammer, J.A.C.K., TrenchBroom, and Q3Edit features.
Its proposed 2D tools and geometry extensions are design priorities, separate
from the implemented controls documented here.
