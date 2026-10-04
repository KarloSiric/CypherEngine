<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/editor_gui_research.md
//  Purpose: Sizes the Mason GUI against real editors, reports the state of
//           Source 2 Hammer in 2026, assesses what CypherTileEditor and the
//           current editor framework already give Mason, and proposes the
//           GUI plan and budget for approval.
//  Details: Capability comparisons (tile rules, meshes, prefabs, paths,
//           build loop) live in editor_comparison_beyond_trenchbroom.md;
//           this document is about the GUI: how big it is, how it is built,
//           and in what order ours gets built. Measured numbers say how
//           they were measured; everything else is marked as knowledge or
//           recommendation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Mason GUI Research and Plan

**Date:** 2026-09-29
**Status:** research complete; the plan in section 7 was approved on
2026-09-29 and phase G1 is under way (progress in section 7.1).

## Hammer 5 look and editor gap analysis — 2026-10-02

Sources read: Valve's Source 2 level design docs (Hammer Overview,
Navigation, Creating Your First Room), the TrenchBroom 2026.2 reference
manual (inspector, info bar, grid, context menu, filtering, hiding, locking,
layers, issue browser, command repetition, view layouts), and NetRadiant's
menu structure (its `radiant/mainframe.cpp` menu labels; ideas only, no
code). A Hammer screenshot was measured region by region.

**Hammer 5's look, as applied.** Measured pixel by pixel from Valve's
full-size Hammer screenshot (2561 px wide): toolbars #262626; buttons a
flat #323232 face with a light #555-#606060 rim; a checked tool or mode is a
lighter #555555 face with an orange #bf773b rim and orange label, never an
orange fill. Mason: `ui.chrome` toolbars, `ui.button` = chrome + 5 % text,
`ui.checked` = button + 18 % text, accent rim and `ui.accent.light` label.
Icons follow Hammer's: modelled objects in dark steel with light edges,
glyphs in light grey, every hue muted to about a third, the amber
"selected" accent kept, and run/build/diagnostic icons keeping their
colour (`generate_editor_icons.py`, tone pass). Viewports: one dropdown in
the top-left corner naming the view, holding every option (projections
with their keys, "2d Top  F2"; camera or navigation; drawing aids;
background; frame, maximize, hide; Asset Browser and Object Properties);
the object under the pointer draws in Hammer's hover green, the selection
in yellow, and 2D geometry in one neutral grey with muted entity tints;
the red outline follows the pointer. The right column is Auto Vis Groups
over Selection Sets beside the Outliner (Undo History as its neighbouring
tab) over Object Properties. Assets open in the Asset Browser window, not
a dock.

**View layouts.** All sixteen arrangements of one to four panes, picked
from an icon grid on the Layout button (Hammer's "Layout:" control); the
`.cylayout` `arrangement` member names all of them.

**Gaps, and where they stand.**

| Feature | Seen in | State |
| --- | --- | --- |
| Command History panel with Repeat (Shift+G) | Hammer, TrenchBroom (command repetition) | done: Window > Command History, `edit.repeat_command` |
| Keyboard Shortcuts reference (every command and its keys, searchable) | Hammer (F1), NetRadiant (Shortcuts) | done: Help > Keyboard Shortcuts (Ctrl+/) |
| Map Info (counts by kind and class, materials) | NetRadiant (Map Info), TrenchBroom (map inspector) | done: Map > Map Info, with Select All of Class |
| Go To object by ID or name; Go To position | NetRadiant (Find Brush), TrenchBroom (Move Camera To) | done: Map > Go To (Ctrl+Shift+G), `map.go_to` in the console |
| Status bar selection size and position, `64w 96l 32h @(x y z)` | Hammer | done |
| F2 / F3 / F4 and Ctrl+Space: projection of the active view | Hammer | done (Control+Space on macOS) |
| Outliner: include hidden objects, show node IDs | Hammer | already present (Outliner Filter menu) |
| Welcome window with recent maps; File > Open Recent | TrenchBroom | done (`editor.ui.show_welcome`) |
| Background image under a 2D view (tracing) | NetRadiant | done: view menu > Background Image (per pane and projection) |
| Check for Problems (Alt+P) with Go to Error | Hammer, TrenchBroom (issue browser) | done: outputs, classes, player start, stacked entities, broken brushes, far objects, missing assets |
| Issue fixes and hidden issues | TrenchBroom | planned with map editing operations |
| Auto Vis Groups presets (Load / Save) | Hammer | done (`editor.map.visgroup_presets`) |
| Layers: hide, lock, omit from export, current layer | TrenchBroom, NetRadiant | planned with layer editing |
| Region / cordon | NetRadiant, Hammer | done: Map > Edit Cordon / Toggle Cordon (dimmed outside, dashed box) |
| Asset Browser window (tabs, filter, saved searches, list / grid / tree, Asset Types, Mods, status badges, Accept) | Hammer | done: Active Material > Browse picks, Fast Asset Search feeds it, `assets.browser` |
| Hover highlight in every view (green), selection yellow | Hammer | done (`editor.viewport.hover_highlight`) |
| Minimal view tab: the view's name and one menu | Hammer | done |
| Grey, mature icon set with minimal colour | Hammer | done (tone pass in the icon generator) |
| Database View: material properties typed by the shader's interface, folder buttons, shader editor (GLSL highlighting, New Shader), texture panel (zoom, channels), entity inspector, generic recipes | CryEngine Sandbox | done: Texture / Window > Database View, `assets.database [path \| #id]`, folders in Object Properties and Active Material |
| Nudge selection with the arrow keys; arbitrary rotate / scale | NetRadiant | planned with transform operations |
| Lift / apply material (Ctrl+M, Shift+T), Fullbright / lit (F5 / F6) | Hammer | waits for the renderer and face editing |

## GUI refinement checkpoint — 2026-10-01

The revised composition keeps the original two-column tool palette,
with Tool Properties over Active Material on the left;
a large perspective viewport over two orthographic panes; visible Undo History
beside Outliner/Selection Sets on the right; Object Properties below those.
Assets and diagnostic panels share tabs below the editing area.

The official Dota 2 site hosts Valve's
[Hammer overview](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm).
Its documentation supports perspective-led editing, context-sensitive tool
properties, dockable panels, and history/outliner/selection-set workflows.
The supplied screenshots are the visual reference for compact chrome and
grey idle glyphs with orange selected glyphs. Mason uses original SVG assets;
no Valve artwork or implementation is copied.

Toolbar icons use neutral idle glyphs and warm checked-state shading,
including grid, snapping, visibility filters and panel toggles. The checked
surface has a restrained warm fill; disabled commands remain subdued. Main
icons default to 28px and the two-column palette to 32px. The upper geometry
row groups mesh, Boolean and surface operations using the same actions as the
menus; unwired operations remain disabled. Window > Toolbars can hide or
restore these rows, with visibility saved in the workspace.

The shell now offers four viewport arrangements through the Layout chooser,
Window menu, and command palette: Perspective + Two Views, Four Views, Two
Views, and Single View. Switching arrangements retains view widgets and their
camera state. Projections can still be changed independently, including two
Top views. Window also exposes every support panel so closing one is reversible.

Undo History is connected to the shared document history, including undo,
redo, row navigation, the clean marker, and transaction availability. Selection
changes are not inserted into the document history: doing that without a
separate edit-state policy would incorrectly mark maps modified. The panel does
not imply that currently disabled geometry interaction commands are implemented.

`workspace.json`, beside `editor.cysettings` in Mason's platform configuration
folder, stores local window geometry, dock/tab placement, viewport types,
visibility, maximization, and divider sizes. It is written atomically on a
confirmed window close or **Save Workspace**. **Restore Saved Workspace** loads
it; **Reset Layout** returns to the bundled `.cylayout`. It is local Qt/ADS UI
state, not a new portable map format. Tests use isolated temporary directories.
Camera poses themselves are retained during an editing session but are not
serialized in this layout file.

The numeric viewport overlays are controlled independently by
`editor.viewport.show_metrics`, default false. Existing ruler, axis, active
border, and hover settings remain separate. The adaptive orthographic grid
uses `editor.grid.min_spacing_px` (12px by default), independently of the
authored snap size. Theme metrics and colours remain
editable; toolbar geometry is not baked into icon bitmaps.

Selection bounds, world-axis dimensions and vertex markers have independent
settings: `editor.viewport.show_selection_bounds` (true),
`editor.viewport.show_selection_dimensions` (true) and
`editor.viewport.show_selection_vertices` (false). Entity output overlays use
`editor.viewport.io_lines` (never/selected/always). Lines represent authored
CYMAP output targets, including duplicate-name fan-out and supported name
prefixes. Runtime targets and entities without a defined spatial endpoint do
not receive invented positions.

This checkpoint concerns the shell and working UI state. Mason's current map
preview remains a wireframe view. It does not establish complete renderer
integration, playable map preview, editable entity schemas, or complete
geometry-tool interaction. Those require their own end-to-end verification.

Native verification note: macOS accessibility inspection of the installed Qt
6.11.1 build produced a crash in `QMacAccessibilityElement::accessibilitySelectedChildren`.
Its stack and local plugin disassembly strongly match
[QTBUG-149612](https://bugreports.qt.io/browse/QTBUG-149612) and the proposed
[Qt ownership fix](https://codereview.qt-project.org/c/qt/qtbase/+/765434).
This attribution has not been confirmed with an independent minimal reproducer.
Normal launching and offscreen GUI checks remain usable. No installed Qt files
were patched and no accessibility feature was disabled in Mason.

## Menu and command discovery pass — 2026-10-01

Primary references reviewed for this pass:

- [TrenchBroom reference manual](https://trenchbroom.github.io/manual/latest/): the
  tool-sensitive information strip, View Options dropdown, contextual selection
  actions, inspector, and customizable shortcuts.
- [TrenchBroom ActionManager](https://github.com/TrenchBroom/TrenchBroom/blob/master/lib/TbUiLib/src/ActionManager.cpp):
  menus group transforms, CSG, vertices, patches and materials, while the toolbar
  references the same actions rather than implementing a second command path.
- [NetRadiant Custom mainframe source](https://github.com/Garux/netradiant-custom/blob/master/radiant/mainframe.cpp#L932):
  View and Modify submenus organize camera, orthographic, filter, region,
  component and transform commands.
- [Valve's Hammer overview](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm):
  selection modes, contextual tool properties, viewport layout and dockable panels.

Mason changes apply these organizational ideas through its existing command
registry. No third-party implementation or icon artwork was copied. The original
28-tool, two-column palette remains visible. File/Edit/etc. appear inside the
window by default, including macOS; `editor.ui.native_menu_bar` opts into the
system menu instead. Long menus use task subgroups. Upper Select, Editing, View,
Mesh, CSG and Surface labels are immediate left-click menus beside their quick
icons. Menus reuse the same QAction objects, shortcuts and availability states.

Viewport titles and View buttons expose projection, framing, maximize/close and
drawing aids. Drawing settings explicitly apply to all views; framing and pane
layout controls affect the named pane. Empty-map left-click selection and camera
right-drag navigation retain their established roles. Panes keep independent
navigation: TrenchBroom's linked-view behavior is not the default here.

The menu bar and extra icons are discovery mechanisms, not evidence that every
listed backend is connected. Commands for unwired operations remain disabled.

Validation for this pass: Mason and the five editor test executables build;
all five editor suites pass. Regression checks exercise actual left-click menu
opening, shared QAction state, drawing-setting changes, pane controls and
unchanged selection/camera state while menus open. The offscreen screenshot
fixture passes 28 assertions and renders both workspace layouts and popup
menus for visual inspection. These are Qt offscreen checks, not a native
macOS accessibility test; the accessibility limitation above still applies.

## Viewport and object-discovery polish — 2026-10-01

The palette retains all 28 earlier entries and adds eight registered operations
(inset, bevel, bridge, fill hole, hollow, carve, brush merge and brush-to-mesh).
Groups have visual separators, without category labels. A vertical scroll area
keeps the two-column palette accessible at small window sizes. Added entries
retain the registry's actual enabled state; this is not geometry-tool integration.

Checked buttons have orange borders and shaded warm faces; inactive icons stay
neutral and retain their silhouettes. Larger default main icons and padding
remain theme-controlled. View maximize is a checked action, shared by its icon
and menu entry, and returns to the unchecked state when its layout is restored.

The 3D ground grid follows the camera with three nested adaptive levels. It is
world aligned, clipped to the near plane and viewport, and bounded to 774 line
candidates per paint. Grid density is separate from the authored snap step.
World X/Y/Z axes remain at the true origin, independent of the camera-relative
grid footprint. Orientation cues show all three axes in every projection.

Angle and scale preferences have separate icons, split dropdowns and status
controls. Off/on restores the last positive step during the session; custom
steps entered in Settings remain valid. Preferences resolve through the same
scopes as other settings. These controls prepare tool constraints; rotation
and scaling interaction still require their geometry tools to be connected.

The outliner displays the map, layers, authored groups, entity ownership and
typed geometry/entity icons. Its Mapnode filter supports text, object kinds,
hidden-object inclusion and selected-only inspection. Filtering the tree does
not discard selection outside the visible subset. Group rows are read-only
until group editing is connected. Filters survive in-session scene rebuilds;
they are not yet serialized with workspace presentation.

Validation: Mason and all five editor test targets build successfully. The
editor core, map core, GUI, map GUI and Mason smoke suites all pass. New checks
cover the 36-entry palette at laptop size, actual menu/action synchronization,
scoped snapping settings, finite grid input, far-camera floor coverage,
maximized-view state, mapnode hierarchy/filtering and originless hidden entities.
Rendered style checks verify neutral icon detail, checked borders and split
dropdown backgrounds. The screenshot fixture passes 35 assertions and captures
both layouts plus the view, snapping and outliner menus. Verification uses Qt's
offscreen platform; it does not establish native macOS accessibility coverage
or full engine-renderer integration.

## Corrective interaction and presentation pass — 2026-10-01

This checkpoint supersedes the earlier colored-band and warm-icon treatment.
The accepted two toolbar rows, two-column palette, and dock arrangement stay.

- Checked icons blend back their own original steel, blue, tan, and other
  artwork colors at strength 0.65; idle icons are gray and hover uses 0.35.
  Checked fills are neutral and the border uses the theme accent. Changing
  the accent no longer repaints the whole icon.
- Colored grid bands and their settings/menu actions are removed. The
  perspective floor uses one neutral bounded lattice rather than overlapping
  levels, with distant projected lines fading before they become subpixel noise. The display minimum is 4 px. Major spacing follows the authored grid
  size, so adaptive coarsening cannot erase every visible indication of a
  size change. Compact numeric rulers remain toggleable. Pane controls move
  to the top-right with the checked maximize state retained.
- Main menus list commands directly with separators. Mesh and Texture no
  longer nest their operations in extra submenus.
- Fast Asset Search, sound, settings, and the command palette occupy the
  top-right utility toolbar. Search clears kind/folder restrictions, preserves
  Sources, reveals the asset browser and focuses its first result on Enter.
  Query text stays synchronized between both search fields; live typing
  retains main-window focus even with a floating browser. Browse Materials
  clears unrelated search/folder restrictions.
  The sound control persists a preference for the future preview backend.
- Tool Properties use grouped setting and operation descriptors, visible
  keyboard guidance, and compact icon groups for texture operations. Real
  command availability stays authoritative. Currently Select and Camera are
  the enabled tool interactions; disabled geometry and UV operations are not
  presented as implemented.
- Asset tiles use neutral presentation with a selected outline, without
  colored per-kind underlines. Folder hierarchy connectors remain visible.
- The application mark is a dressed ashlar stone block and chisel with a
  drafting guide, replacing the rejected letter monogram.

All five editor suites pass after these changes, covering checked source
colors, disabled states, grid changes, header placement, flat menu discovery,
quick search, floating-browser focus, synchronized queries, and sound state.
The screenshot fixture passes 44 assertions. Reviewed captures include
1280×720, 1600×1000 and 2560×1600, with the user’s custom Slate theme as well
as the bundled themes. Utility controls remain visible and adjacent at all
three widths. The current viewports
still draw wireframes with QPainter; this GUI pass does not add a textured
renderer, geometry editing kernels, or audio playback.

## Two-row composition and configurable detail pass — 2026-10-01

The five upper toolbars now share two rows: general commands and visibility
on the first; selection, editing and geometry on the second. Older saved
toolbar placements normalize after restoration without resetting docks or
viewports. `editor.ui.two_toolbar_rows` disables this normalization for custom
placements. At narrow widths Qt's overflow menus keep actions reachable.
The two-column 36-tool palette has compact group dividers, no empty stretch
row, and a scroll area for shorter windows.

Idle icons remain neutral. Enabled hover introduces a subtle amount of the
original illustration colors; checked icons retain warm faces and orange
borders. Disabled icons and buttons remain visually disabled on hover.
`ui.icon.hover_color_strength`, `ui.tool_strip.spacing` and
`ui.tool_strip.separator_height` expose these details in the theme catalogue.

Minor, major and contrasting band grid lines are distinct in all views.
`editor.grid.major_every` now controls major spacing; `editor.grid.bands` and
`editor.grid.band_interval` control world-aligned bands (16 drawn cells by
default, configurable from 2 to 128). The dedicated theme color is
`viewport.grid.band`. Drawing density remains separate from authored snap
size. The perspective floor defaults on, can be hidden independently of
axes, and retains the bounded adaptive footprint. Coordinate rulers remain
compact overlays and avoid pane headers and orientation widgets.

Asset previews default to 144 logical pixels, with a 48–256 slider and size
presets. The default layout gives assets enough height for previews and
captions. Size and grid/list presentation persist as
`editor.assets.thumbnail_size` and `editor.assets.list_view`; changing modes
retains filtering and the current asset. Decoding retains up to 512 pixels
for a 256-pixel preview at 2× display scaling. Outliner branch connectors are
controlled by `editor.ui.tree_lines`; `editor.outliner.show_ids` optionally
appends IDs to named nodes. Type, entity class and ID stay in tooltips.

Mason has an original vector application mark and a macOS icon bundle.
The startup screen derives its construction drawing and typography from the
active theme, displays the build version and actual initialization stages,
and has no artificial delay. Its backing pixmap follows display pixel ratio.
`editor.ui.show_splash` hides it; `editor.ui.start_maximized` now affects launch.
The macOS build produces `bin/Mason.app` while retaining the `bin/Mason`
command-line entry point.

Validation: Mason and the five editor test executables build successfully.
All five editor suites pass. Added checks cover toolbar rows at 2560, 1600
and 1280 pixels, restored three-row placements, hidden toolbar preservation,
live asset presentation, hierarchy connectors and ID filtering, grid settings,
hover/disabled icon states, startup callbacks and Retina splash dimensions.
The screenshot fixture passes 37 assertions and captures both layouts, a
1280×720 presentation, the startup image and popup menus. The command-line
executable also starts and opens the example map on Qt's offscreen platform.
These checks do not establish native accessibility coverage or full modeling
tool/renderer integration.

## 1. Summary

- **A full map editor GUI is about 100k lines.** TrenchBroom, the closest open
  comparison, has about 64k lines of Qt UI and about 30k lines of tools and
  interaction logic, plus about 30k lines of viewport rendering. The
  owner's 100k estimate for Mason's GUI and tooling matches that.
- **Mason today:** about 5.3k lines of framework and GUI (EditorCore 3.3k,
  EditorGui 2.2k, Map GUI 2.0k, Mason app 0.8k), on top of 5.6k lines of map
  core and 87.6k lines of geometry. The backend the GUI needs to edit maps
  already exists; the GUI is what is missing.
- **CypherTileEditor has about 20k lines of Qt GUI** that solve many of the
  same problems: a real OpenGL viewport through CypherRender, a four-view
  workspace, a data-driven settings dialog, undo history, a material
  browser, and a build and run runner. ADR 0008 already plans to move it onto
  the shared framework; most of it becomes Mason's foundation.
- **Formats:** every format the GUI reads or writes is specified, and most
  are implemented. The GUI-side gaps are the V2 decoders for theme, keymap,
  and layout, and the `.cygame` decoder that property editing needs
  (section 5).
- **Hammer 5:** alive but quiet. Valve ships tool fixes inside Counter-Strike
  2 updates; there is no public Source 2 SDK; Deadlock has no official tools;
  Facepunch's s&box shipped in April 2026 with its own mapping tool replacing
  Hammer for new levels (section 3).
- **Ready for the GUI:** yes, once four framework pieces land first (docking,
  undo and selection, the real viewport host, and an icon set), because
  every later panel depends on them (section 6).

## 2. How big other editors' GUIs are

### 2.1 TrenchBroom (measured)

Measured on 2026-09-29 from the GitHub tree of `TrenchBroom/TrenchBroom`
(`master`): the byte size of every `.cpp`/`.h` file, grouped by library,
divided by 24.5 bytes per line. That divisor was measured on two files,
`MapWindow.cpp` (65,890 bytes, 2,629 lines) and `MapWindow.h` (10,072 bytes,
426 lines). Figures include each file's GPL header (about 19 lines).

| Library | What it is | Code (lines) | Tests (lines) |
| --- | --- | ---: | ---: |
| TbUiLib | Qt widgets: window, views, inspectors, browsers, dialogs, preferences | ~64k | ~7k |
| TbAppLib | Tools and their controllers, input handling, MapDocument | ~30k | ~14k |
| TbRenderLib + TbGlLib | Viewport rendering and OpenGL wrappers | ~30k | ~12k |
| TbMdlLib | Map model: nodes, brushes, entities, parsers, commands, validators | ~92k | ~85k |
| KdLib, VmLib, TbElLib, TbFsLib, others | Utilities, vector math, expression language, file system | ~60k | ~50k |

What TrenchBroom's UI contains (from its `TbUiLib/src` file list):

- **Window and actions:** `MapWindow` (~2.6k lines), `ActionManager` (every menu entry
  and shortcut in one table, ~3k lines), `AppController`, `WelcomeWindow`,
  `RecentDocuments`, `CrashReporter`, `AboutDialog`.
- **Views:** `MapView2D`, `MapView3D`, `MapViewBase`, one- to four-pane
  containers, `CyclingMapView`, `FlyModeHelper`, `CellView` (thumbnail grids),
  `UvView` / `UvEditor`, camera and selection flash animations.
- **Inspectors:** `MapInspector` (layers, mods, map properties),
  `EntityInspector` with `EntityPropertyGrid` / `EntityPropertyModel` and
  smart editors (choice, color, flags, WAD), `FaceInspector` with
  `FaceAttribsEditor`, and a tool page per tool (rotate, scale, sweep, draw
  shape, control points).
- **Browsers:** entity browser, material browser, issue browser
  (validation), `LayerEditor`, `MaterialCollectionEditor`.
- **Build loop:** compilation profiles, `CompilationRunner`, task list,
  launch-game-engine profiles and dialog.
- **Preferences:** colors, games, keyboard (shortcut model and key sequence
  editing), mouse, view, update.
- **Small widgets:** titled and collapsible panels, tab book, search box,
  spin control, slider with label, popup button, color button, elided
  label, border line.

Notes for Mason:

- TrenchBroom deliberately uses a fixed splitter layout, not free docking.
  Mason has already chosen docking (Qt ADS), which TrenchBroom never needed.
- TrenchBroom keeps tools (TbAppLib) separate from widgets (TbUiLib). This is
  the same split as our Map Core / Map Gui, and it is what keeps its tools
  testable.
- TrenchBroom is GPLv3. It is studied for ideas and sizing only; no code is
  copied ([reference_policy.md](reference_policy.md)).

### 2.2 Other editors

| Editor | GUI technology | Size of the editor GUI | Source |
| --- | --- | --- | --- |
| Godot | Its own GUI toolkit | `editor/` is about 12.1 MiB of C++ (roughly 400k+ lines), module editors add 2.4 MiB, and the toolkit (`scene/gui`) 3.4 MiB | Measured from the GitHub tree, sizes only; the line figure is an estimate |
| CryEngine Sandbox (5.x) | Qt (moved off MFC), its own docking manager, EditorCommon library | Not measured: `CRYTEK/CRYENGINE` returns 404 on GitHub today | Knowledge; ideas only under Crytek's license |
| O3DE Editor | Qt with AzQtComponents and AzToolsFramework | Not measured: the tree is too large for the API in one request | Knowledge |
| Hammer 5 (Source 2) | Qt, rebindable hotkeys, dockable panes | Closed source | Wikipedia; Valve documentation |
| Unreal Editor | Slate (its own toolkit) | Millions of lines; not comparable | Knowledge |

Takeaway: a focused level editor lands around TrenchBroom's size (about 100k
GUI-facing lines). A general engine editor like Godot is four to five times
that. Mason's ambitions (Hammer 5 look, Sandbox depth) sit between the two;
100k is right for the map editor, and every further workspace (material,
particle, model, sequencer) adds to it.

## 3. Source 2 Hammer (Hammer 5) in 2026

**History.** An alpha of Hammer 5 shipped with the Dota 2 Workshop Tools in
2014. The current generation arrived with the Half-Life: Alyx Workshop Tools
in 2020: an overhauled Qt UI, a WYSIWYG viewport, new file formats (`.vmap`),
and one Resource Compiler replacing VBSP/VVIS/VRAD. It ships today as part of
the Counter-Strike 2 Workshop Tools
([Wikipedia: Source 2](https://en.wikipedia.org/wiki/Source_2)).

**Where it stands (September 2026).**

- **Valve:** no standalone Hammer release and no public Source 2 SDK. Tool
  changes ride along in CS2 updates. For example, the July 29, 2026 update
  fixed map scripts failing to load in tools mode, and recent builds added
  entity definition files documenting inputs, outputs, and properties
  ([CS2 updates](https://www.counter-strike.net/news/updates),
  [CS2News.gg](https://cs2news.gg/)).
- **Deadlock:** Valve has released no official tools. The community's CSDK 12
  repackages the CS2 Workshop Tools (Hammer, ModelDoc, Source Filmmaker,
  Resource Compiler) for Deadlock
  ([Deadlock modding tools](https://deadlockmodding.pages.dev/modding-tools/),
  [modlock-sdk](https://github.com/paralin/modlock-sdk)).
- **s&box (Facepunch):**
  - It released on Steam on April 28, 2026. Its C# layer has been MIT-licensed
    since November 2025; Source 2 underneath stays proprietary
    ([Wikipedia: s&box](https://en.wikipedia.org/wiki/S%26box),
    [sbox-public](https://github.com/Facepunch/sbox-public)).
  - In December 2025 Facepunch moved mesh editing into its own Scene Mapping
    tool. That tool has Hammer's shortcuts and fast texturing, and new levels
    are saved as `.scene` files. Hammer stays bundled for `.vmap` work
    ([s&box mapping docs](https://sbox.game/dev/doc/editor/mapping/),
    [update 25.12.10](https://sbox.game/news/update-25-12-10/)).
  - The Linux build (September 2026) ships without Hammer, ModelDoc, or
    AnimGraph. Reception so far is mixed
    ([GamingOnLinux](https://www.gamingonlinux.com/2026/09/s-box-from-facepunch-now-has-native-linux-support-on-steam/)).
- **Community:** third-party tools fill gaps Valve leaves. One example is
  Hammer 5 Tools, which provides SmartProp, sound event, hotkey, and loading
  screen editors and exposes headless CLI and MCP modes for AI agents
  ([Hammer5Tools](https://github.com/dertwist/Hammer5Tools)).

**What Hammer 5's UI is (knowledge).** The Valve Developer Community pages
sit behind a bot check that was not bypassed, so the following is not
re-verified today:

- Selection modes on number keys: vertex, edge, face, mesh, object.
- Transform modes on W/E/R.
- Tools on Shift+letter: block, entity, polygon, path, tile, clip, texture
  (fast texture and hotspot).
- Right-side Outliner, Object Properties (with Outputs and Inputs), selection
  sets, and auto visgroups.
- Separate Asset Browser and Material Editor windows.
- F9 builds and runs.
- s&box's mapping tool keeps the same key layout: 1–5 for vertex, edge, face,
  texture, and object, and W/E/R/T/Y for position, rotate, scale, pivot, and
  resize.

**What Mason should take from it.**

1. **Mesh-first selection modes on number keys**, beside the brush workflow.
   Our geometry library already has meshes with stable IDs.
2. **Fast texturing:** a face-texture tool with hotspot-style snapping.
3. **Asset browser as the hub:** one browser for materials, models, prefabs,
   and sounds, dockable or floating (the owner's setting).
4. **Entity I/O editing:** Outputs and Inputs tabs, validated against the
   game definition (`.cygame`).
5. **Geometry and gameplay in one document.** s&box moved to this
   (`.scene`). Our `.cymap` already does it, so Mason does not need a
   separate map-instance bridge.
6. **Rebindable everything, one build key.** Our command registry and keymap
   already work this way.

## 4. CypherTileEditor: what Mason inherits

CypherTileEditor is about 27.5k lines: 20.0k Qt GUI and 7.4k core (measured
with `wc -l` today).

| TileEditor piece | Lines | What Mason does with it |
| --- | ---: | --- |
| `CypherTileRenderViewport` | 2,009 | **The model for Mason's 3D view:** `QOpenGLWidget` drawing through CypherRender. Replaces the current QPainter stand-in. |
| `CypherTileViewWorkspace` | 876 | Hosts four views without recreating widgets or cameras; merges with Mason's view arrangement (maximize, cycle). |
| `CypherTileEditorSettingsDialog` + preference fields | 1,613 | Data-driven preferences; generalised into a settings dialog generated from settings each module registers (ADR 0008 contract). |
| `CypherTileHistoryPanel` | 450 | The undo history panel, once EditorCore has its undo stack. |
| `CypherTileMaterialBrowser` | 602 | Material discovery, compiling, and thumbnails; grows into the asset browser's material tab. |
| `CypherTileShellRunner` | 866 | Asynchronous build and run with output; becomes the compile and run profiles runner. |
| `CypherTileOrthoView` / `Canvas` | 3,605 | Tile-specific canvas; its camera, grid, and zoom behaviour inform the map 2D views. |
| `CypherTileEditorMainWindow` | 4,273 | A monolith; split into framework commands, menus, and panels during migration. |
| Theme, user themes, config, icons, colors | ~1,700 | Already superseded by EditorCore themes and settings; TileEditor's INI files migrate to `.cytheme` / `.cysettings`. |

Gaps to fix during migration: QSettings/INI persistence, some headers
without the standard header block and asserts, and the monolithic main
window.

## 5. Are the formats in place?

| State | Formats |
| --- | --- |
| **Implemented and tested** | CYKV V1/V2 (Tier1); `.cymap` V10 with chunks, now including disk load and staged save; `.cytilemap`; `.cysettings` V1/V2; `.cyproject` V1/V2; `.cyworkspace` V1; `.cytheme` V1; `.cykeymap` V1; `.cylayout` V1; `.cyfont` V1; `.cyshader` / `.cytex` / `.cymat` with cooked forms; CYRS; `.cypak` V10; `.cfg` / `.cycfg` |
| **Specified, decoder not written** | `.cytheme` / `.cykeymap` / `.cylayout` V2; `.cyworkspace` V2; `.cyproject` V3; `.cyfont` V2; `.cygame` V1; `.cyplugin` V1; `.cyprefab` V1 (CYMAP §6.12); `.cydf` |
| **Planned with their subsystems** | `.cymesh`, `.cyskel`, `.cyanim`, `.cyparticle`, `.cysnd`, audio events and mixer, `.cyinput` / `.cybindings`, `.cyui`, `.cyflow`, `.cyphys`, `.cynav`, `.cycine`, save and replay records |

For the GUI specifically, only four decoders are needed soon:

- the V2 theme, keymap, and layout decoders (full keymap, toolbar and window
  layouts, theme choices);
- the `.cygame` decoder, which drives entity classes, property types, smart
  editors, and I/O validation.

Everything in the last row belongs to runtime subsystems that do not exist
yet and does not block the GUI.

Documentation corrections found during this review, made on 2026-09-29:

- `FORMAT_CATALOG.md` contradicted itself on maps (row 53 named `.cyscene`
  as the compiled output, row 99 said the chunk layout was not frozen); both
  rows now match ADR 0009 and the implementation.
- `FORMAT_CATALOG.md` listed `.cyprefab` as Planned and `.cyplugin` as
  Proposal; both now read Specified.
- `map_authoring_and_mason.md` still uses the old `.cyscene` name in its
  body, but its status block already points to ADR 0009; no change needed.

## 6. Readiness for the GUI

Ready, with four foundation pieces first. Each one would otherwise be
retrofitted into every panel built before it:

1. **Qt Advanced Docking System** in place of QDockWidget (download
   approved; now pinned at release 5.1.1, LGPL-2.1, built as a shared
   library). It brings
   tabbed and floating panels, saved perspectives, and floating browser
   windows (the owner's "docked or separate window" setting).
2. **Undo/redo and selection in EditorCore.** Every editing panel and tool
   needs transactions, labels, and a history. TileEditor's history model and
   the geometry library's transactions are the starting points.
3. **The real viewport host:** a `QOpenGLWidget` through CypherRender, as in
   TileEditor, so the 3D view renders materials instead of lines. The 2D
   views stay QPainter until profiling says otherwise.
4. **An icon set.** Mason needs hundreds of icons; the bundled Tabler subset
   has 33. The full Tabler set (MIT, about 5,000 SVGs) covers the whole
   editor in one consistent style. Downloading it needs approval.

## 7. Proposed plan and budget (for approval)

The phases are ordered so each one only uses what the phases before it
built. The line budgets are estimates, calibrated against TrenchBroom and
TileEditor.

| Phase | Contents | Budget |
| --- | --- | ---: |
| **G1. Framework completion** | ADS docking and perspectives; undo/redo and selection core; V2 theme/keymap/layout decoders; generated settings dialog; theme editor; keymap editor; command palette; welcome window and recent files; notifications; TileEditor migration onto the framework | ~15k |
| **G2. Viewports** | GL viewport host (CypherRender); camera presets (Hammer, TrenchBroom, Blender); 1–4 pane arrangements with maximize and cycle; grid and snapping; transform gizmos (the Mathlib gizmo math exists); marquee and lasso selection; render modes (wireframe, flat, textured, lit) | ~15k |
| **G3. Map tools** | Hammer 5 selection modes (vertex/edge/face/mesh/object); block and primitive, clip, vertex, polygon, path, entity placement; face texture tool with UV editor; CSG commands; terrain sculpt and paint; foliage painting; measure, align, and array commands | ~30k |
| **G4. Inspectors and browsers** | Object properties with smart editors from `.cygame` (choices, flags, colors, target pickers, Hammer-style I/O); face inspector; asset browser (materials, models, prefabs, sounds); entity browser; layers, visgroups, and selection sets; outliner upgrades; history panel; problems browser for map checking; find and replace; entity report | ~22k |
| **G5. Workflows** | Build and run profiles with output and error navigation; game configuration (`.cygame`); project and workspace manager; autosave and recovery journal; live chunk reload | ~10k |
| **G6. Further workspaces** | Material, particle, and model editors, each a workspace on the same framework | ~10k to start |
| **Total** | | **~100k** |

Each phase ends with a check-in, tests, and screenshots, so layout and
styling are reviewed piece by piece.

### 7.1 G1 progress

| Piece | State |
| --- | --- |
| ADS docking (main and floating containers, layout capture and apply) | Done |
| Undo/redo history and selection (EditorCore) | Done |
| V2 theme, keymap, and layout decoders | Done |
| Settings registry, scopes, and generated settings dialog | Done |
| Command palette (fuzzy matching, recent commands) | Done |
| Theme editor (live preview, formula colours, complete root themes, plugin tokens kept) | Done |
| Mason look v2: TileEditor base (pane headers, rulers, axis triads, black views, uppercase docks) with Hammer 5's panels (Tool Properties, Active Material, Auto Vis Groups, Selection Sets, Object Properties) and toolbars (Select:, View:), SketchUp-style two-column tool palette | Done (2026-09-30, first pass; to be refined) |
| Colour icon set: 217 generated icons (`tools/asset_content/generate_editor_icons.py`, v2: small 3D models, isometric, shaded), SVG sources plus 24/32/64 px PNGs | Done (second set) |
| Console, Output, and Problems on one log store (boot report, level and channel filters, status-bar counters) | Done (2026-09-30) |
| Settings > Appearance (TileEditor-style theme studio: theme list with 11 bundled presets, interface scale, every colour grouped with swatch, Auto, and revert; custom settings pages) | Done (2026-10-01) |
| Asset browser (Hammer 5 style, bottom centre): Qt-free catalogue (`CypherEditor_AssetCatalog`), kind tabs, folder tree, fuzzy search, sources toggle, grid/list, real thumbnails through material and texture recipes, Used in Map with missing assets, activation sets the Active Material | Done (2026-10-01, first pass; G4 adds previews, drag to viewports, import) |
| Keymap editor | Core and library done; dialog next |
| Welcome window and recent files | Pending |
| Notifications | Pending |
| TileEditor migration onto the framework | Pending |

## 8. Sources

- TrenchBroom repository tree:
  <https://api.github.com/repos/TrenchBroom/TrenchBroom/git/trees/master?recursive=1>
- Godot repository tree:
  <https://api.github.com/repos/godotengine/godot/git/trees/master?recursive=1>
- [Source 2 (Wikipedia)](https://en.wikipedia.org/wiki/Source_2)
- [s&box (Wikipedia)](https://en.wikipedia.org/wiki/S%26box)
- [Facepunch/sbox-public](https://github.com/Facepunch/sbox-public)
- [s&box mapping documentation](https://sbox.game/dev/doc/editor/mapping/)
- [s&box update 25.12.10](https://sbox.game/news/update-25-12-10/)
- [GamingOnLinux: s&box native Linux](https://www.gamingonlinux.com/2026/09/s-box-from-facepunch-now-has-native-linux-support-on-steam/)
- [Counter-Strike 2 updates](https://www.counter-strike.net/news/updates)
- [CS2News.gg](https://cs2news.gg/)
- [Deadlock modding tools (CSDK 12)](https://deadlockmodding.pages.dev/modding-tools/)
- [paralin/modlock-sdk](https://github.com/paralin/modlock-sdk)
- [dertwist/Hammer5Tools](https://github.com/dertwist/Hammer5Tools)
- [Qt Advanced Docking System releases](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System/releases)
