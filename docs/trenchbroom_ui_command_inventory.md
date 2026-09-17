<!--
CypherEngine Research Note
File: docs/trenchbroom_ui_command_inventory.md
Purpose: Source-level user-interface, command, shortcut, and pointer-input audit
         used by Cypher editors.
Provenance: Produced by studying TrenchBroom at the pinned revision below.
TrenchBroom is GPL-3.0-or-later. This is not a clean-room specification.
Original analysis © 2026 Karlo Siric; upstream rights remain with their owners.
Do not mechanically translate source-derived material into Cypher.
-->

# TrenchBroom User Interface and Command Surface Inventory

## Table of contents

- [Snapshot, scope, and method](#snapshot-scope-and-method)
- [The central design lesson](#the-central-design-lesson)
- [Command and shortcut machinery](#command-and-shortcut-machinery)
- [Exact main-menu hierarchy](#exact-main-menu-hierarchy)
- [Application shell and persistent layout](#application-shell-and-persistent-layout)
- [Startup and global dialogs](#startup-and-global-dialogs)
- [Toolbar and map-view bar](#toolbar-and-map-view-bar)
- [View Options popup](#view-options-popup)
- [Accelerator hosts and pointer-input precedence](#accelerator-hosts-and-pointer-input-precedence)
- [Viewport navigation, selection, and direct manipulation](#viewport-navigation-selection-and-direct-manipulation)
- [Map-view context menu](#map-view-context-menu)
- [Editing tools and their contextual controls](#editing-tools-and-their-contextual-controls)
- [Inspector inventory](#inspector-inventory)
- [Groups, linked groups, and visibility organization](#groups-linked-groups-and-visibility-organization)
- [Info panel, issue validation, and console](#info-panel-issue-validation-and-console)
- [Preferences: every pane and exposed setting](#preferences-every-pane-and-exposed-setting)
- [Dialog and popup inventory](#dialog-and-popup-inventory)
- [Compilation and launch workflow](#compilation-and-launch-workflow)
- [Status, feedback, and discoverability](#status-feedback-and-discoverability)
- [Enablement and checked-state model](#enablement-and-checked-state-model)
- [What Mason and TileEditor should implement from this research](#what-mason-and-tileeditor-should-implement-from-this-research)
- [Coverage inventory](#coverage-inventory)
- [Mouse and pointer gesture matrix](#mouse-and-pointer-gesture-matrix)
- [Keyboard action inventory](#keyboard-action-inventory)

## Snapshot, scope, and method

This audit is pinned to TrenchBroom commit `e53a0ef172e10e62ff24b86b70ca3b6ea865cac4`. It covers the user-facing editing surface rather than only the prose manual: the main-menu model, the 68 viewport actions and 111 menu actions registered by `ActionManager`, the generated-shortcut pipeline, contextual command predicates and checked state, map-view input controllers, toolbar and popup menus, inspectors, preferences, dialogs, status feedback, issue validation, compilation and launch workflows, and the manual's mouse interaction descriptions. Eight menu actions are debug-only, leaving 171 static actions in a release build. Across all 179 registrations, 115 have explicit nonempty defaults, 50 are initially unbound, 14 request a Qt platform-standard binding, 31 are checkable, and 16 reuse the same action object in the toolbar. Six continuous fly controls live outside the action registry. The appendices catalog all 179 static actions by registry, user-facing command, default binding, and source location, plus 67 concrete map/UV pointer gestures. Runtime-generated tag/entity actions are covered by generation rules because their count depends on loaded game data. Internal predicate and callback expressions are analyzed by behavior rather than reproduced in bulk.

The audit used four layers of evidence:

1. The full 3,083-line manual (`app/TrenchBroom/resources/documentation/manual/index.md`) establishes intended interaction semantics.
2. `ActionManager.cpp`, `Action.h`, `ActionBuilder.cpp`, `ActionContext.*`, `ActionExecutionContext.*`, and `MapWindow.cpp` establish the actual registered commands, IDs, defaults, command predicates, and checked state.
3. Qt UI implementation files establish every visible panel, button, field, popup, dialog, and status indicator.
4. `TbAppLib` tool controllers and the keyboard model establish gesture and mode behavior that does not appear as a menu command.

The source tree was repository-wide search-indexed across 517 directly relevant files and 84,368 lines: 361 `TbUiLib` headers/sources (56,680 lines), 107 `TbAppLib` input/tool headers/sources (21,439 lines), 42 validator headers/sources (2,416 lines), four `DumpShortcuts` files (633 lines), and three manual/build-support files (3,200 lines). The files listed in the final coverage appendix were then read in depth. This is a source audit of the pinned revision; dynamic entity-definition and smart-tag command counts necessarily depend on the game and map loaded at runtime.

## The central design lesson

TrenchBroom's useful lesson is not the count of buttons. Its strength comes from making a large command set coherent through a central action model and a small set of editor states:

- A command owns a persistent preference path, display label, context mask, default shortcut, execute callback, enable predicate, optional checked predicate, optional icon, and status/tooltip text (`lib/TbUiLib/include/ui/Action.h`; `lib/TbUiLib/src/Action.cpp`).
- Menus, the toolbar, the shortcut preferences, and the generated manual all consume the same action objects. A shortcut changed by the user therefore changes menu display, tooltips, and runtime behavior consistently (`lib/TbUiLib/src/ActionBuilder.cpp:31-70`; `app/DumpShortcuts/src/Main.cpp:129-177`).
- Context is three-dimensional: active view (2D/3D), active tool, and selection kind. Commands sharing the same physical key can coexist when their context masks do not overlap (`lib/TbUiLib/include/ui/ActionContext.h:27-54`; `lib/TbUiLib/src/ActionContext.cpp:30-136`).
- Enablement and checked state are live predicates evaluated against the current application, document, map window, and active map view. `MapWindow::updateActionState` updates every QAction centrally, while undo/redo are updated separately with the concrete command name (`lib/TbUiLib/src/MapWindow.cpp:319-397`).
- Game-defined entity classes and smart tags produce dynamic commands. These commands appear in keyboard preferences and are cached per document, then invalidated when the document or entity definitions change (`lib/TbUiLib/src/ActionManager.cpp:59-120`; `lib/TbUiLib/src/MapDocumentActionCache.cpp:31-86`; `lib/TbUiLib/src/KeyboardShortcutModel.cpp:260-363`).

For Mason and TileEditor, this implies that commands should be first-class data before the editors gain hundreds of one-off button callbacks. A command registry should be shared by menu, toolbar, command palette, shortcut editor, documentation generator, automation, and tests. It should expose `canExecute`, `isChecked`, undo metadata, context, and discoverable help text.

## Command and shortcut machinery

### Stable IDs and display paths

The closest thing TrenchBroom has to an action ID is the action's preference path. Examples are `Menu/File/Save`, `Menu/Edit/CSG/Subtract`, `Menu/View/Camera/Focus on Selection`, and `Controls/Map view/Perform clip`. The path stores the user's shortcut preference and is also how the manual resolves `#menu(...)` and `#action(...)` macros. Display hierarchy is independent of the ID: Selection and Groups are top-level menus even though many of their IDs still begin with `Menu/Edit`, and the top-level Tools menu uses IDs under `Menu/Edit/Tools` (`lib/TbUiLib/src/ActionManager.cpp:810-821,1246-1582`).

That separation is valuable for compatibility: menu organization may change without invalidating stored keybindings or documentation references. Cypher should likewise use immutable machine IDs and separate localized labels/menu paths.

### Static and data-driven registry boundaries

`ActionManager.cpp` contains exactly 179 static `Action{...}` definitions: 68 view actions in `createViewActions` and 111 menu actions in the menu builders. Prefixes alone cannot classify them because horizontal/vertical flip and tool deactivation are menu actions whose IDs begin `Controls/Map view/...`. Eight Debug registrations are wrapped in `#ifndef NDEBUG` (`lib/TbUiLib/src/ActionManager.cpp:150-808,810-2053`).

Loaded game data expands that registry. For each smart tag, TrenchBroom creates `Filters/Tags/<tag>/Toggle Visible` plus `Tags/<tag>/Enable` and/or `Tags/<tag>/Disable` when the tag supports those mutations. For each entity definition it creates `Entities/<classname>/Toggle`, and for each non-worldspawn definition it also creates `Entities/<classname>/Create`. With `T` tags, this contributes between `T` and `3T` actions; with `N` definitions including worldspawn, it contributes `2N - 1`. The per-document cache is invalidated on document load, and entity actions are invalidated when definitions change (`ActionManager.cpp:59-120`; `MapDocumentActionCache.cpp:31-86`). The static action appendix therefore remains exactly countable while this data-driven surface is specified by generation rule.

### Action contexts

`ActionContext` uses bit masks with one choice from each dimension:

| Dimension | Values exposed to users or tools |
|---|---|
| View | `View3D`, `View2D`, or `AnyView` |
| Tool | no tool; assemble-brush; clip; rotate; scale; shear; vertex/edge/face as `AnyVertexTool`; control-point; sweep; or any tool |
| Selection | no selection; object/node selection; face selection; or a selection owned by a modal tool |

Fly mode is defined as a 3D view with any selection/tool state. Context matching requires an overlap in all three dimensions. The human-readable keyboard-preferences context is generated from the same flags, producing descriptions such as “3D view, faces selected, no tool” (`lib/TbUiLib/include/ui/ActionContext.h:27-54`; `lib/TbUiLib/src/ActionContext.cpp:30-136`).

### Shortcut persistence, alternatives, and conflicts

Every action shortcut preference stores a vector of key sequences rather than one key. A QAction receives all configured alternatives, and its tooltip appends each native-rendered sequence (`lib/TbUiLib/src/ActionBuilder.cpp:31-70`). The Keyboard pane displays Description, Context, Shortcut, and Alternative columns, supports search and in-place capture, sorts conflicts to the top, marks them in red, and prevents closing until overlapping-context conflicts are resolved (`lib/TbUiLib/src/KeyboardPreferencePane.cpp:45-148`; `lib/TbUiLib/src/KeyboardShortcutModel.cpp:260-378`; manual `index.md:1620-1636`). Action bindings may contain up to four sequential chords; the six continuous fly controls accept one chord (`KeyboardShortcutModel.cpp:225-243`; `LimitedKeySequenceEdit.cpp:39-75`). Caps Lock, Num Lock, and Scroll Lock are rejected as shortcut keys.

Six camera fly bindings are ordinary preferences included alongside registered actions: W/S/A/D/Q/X for forward/back/left/right/up/down (`lib/TbPreferencesLib/include/prefs/Preferences.h:261-272`; `lib/TbUiLib/src/KeyboardShortcutModel.cpp:306-338`). `ActionManager::resetAllKeySequences` resets menu, toolbar, and viewport action preferences; the keyboard pane also resets these plain key preferences (`lib/TbUiLib/src/ActionManager.cpp:128-141`).

### Platform translation and macOS exceptions

Shortcut storage uses Qt portable strings. On macOS, portable `Ctrl` resolves to native Command and portable `Meta` to physical Control; the input layer exposes a cross-platform `CtrlCmd` modifier meaning Command on macOS and Control elsewhere (`lib/TbBaseLib/include/base/KeySequence.h:29-38`; `lib/TbAppLib/include/ui/InputState.h:34-42`). Standard editing shortcuts are obtained through Qt's platform-standard sequences, giving native Command bindings for New/Open/Save/Undo/Copy/Paste and native variants elsewhere. Delete is explicitly Backspace on macOS and Delete elsewhere (`lib/TbUiLib/src/StandardShortcut.cpp:27-72`).

The source has several explicit platform accommodations:

- Maximize Current View uses portable `Meta+Space` on macOS, which renders as physical Control+Space, because Command+Space invokes Spotlight; other platforms use `Ctrl+Space` (`lib/TbUiLib/src/ActionManager.cpp:1903-1918`).
- Automatic Alt mnemonics and Alt-only menu-bar focus are disabled, because Alt+WASD is a core fly-camera interaction (`app/TrenchBroom/src/Main.cpp:119-153`).
- Qt's macOS Control-click emulation is disabled and recreated in the input recorder so Control+left click/drag becomes right-button input reliably (`app/TrenchBroom/src/Main.cpp:289-293`; `InputEvent.cpp:279-313`).
- Shift-wheel camera zoom reads the platform-swapped horizontal axis on macOS, and the event layer separately normalizes Qt's Alt-wheel axis behavior (`CameraTool3D.cpp:277-287`; `InputEvent.cpp:400-425`).

### Generated manual shortcuts

The manual deliberately does not hard-code command keys. Markdown uses `#menu(path)`, `#action(path)`, and `#key(key)` macros. The build runs a headless `DumpShortcuts` executable that visits the same menu, toolbar, viewport actions, and plain camera-key preferences and writes `shortcuts.js`; browser-side helpers turn the IDs into native key names and full menu paths (`app/TrenchBroom/resources/documentation/manual/README.md`; `app/DumpShortcuts/src/Main.cpp:51-177,185-251`; `app/TrenchBroom/resources/documentation/manual/shortcuts_helper.js`; `app/TrenchBroom/cmake/GenerateManual.cmake:3-119`). Qt automatic Alt mnemonics are disabled because Alt+WASD is a fundamental viewport interaction and must not open arbitrary menus (`app/DumpShortcuts/src/Main.cpp:192-196`, with the same policy in the application setup).

### Source-visible gaps in the command machinery

The audit found four implementation gaps that matter when using TrenchBroom as an architectural reference:

1. `actionContextName` omits branches for Scale and Shear. Context matching still works, but those tool bits disappear from the human-readable Context column (`lib/TbUiLib/src/ActionContext.cpp:96-136`).
2. The shortcut conflict key uses context overlap inside an ordered-container comparator. Overlap is not transitive in general, so a pairwise conflict pass or normalized context sets would be easier to prove correct (`lib/TbUiLib/src/ActionInfo.cpp:33-52,105-134`).
3. The preference UI accepts an Alternative binding and up to four chords, but the generated manual renders only `shortcuts[0]`, while `DumpShortcuts::toString` serializes only chord zero. Generated documentation therefore drops alternatives and truncates multi-chord sequences (`app/TrenchBroom/resources/documentation/manual/shortcuts_helper.js:15-35`; `app/DumpShortcuts/src/Main.cpp:76-126`).
4. Pan, Zoom, and Rotate native gesture events are represented and routed through the entire controller chain, but no concrete controller overrides `acceptGesture`; the gesture path is dormant in this revision (`lib/TbAppLib/include/ui/InputState.h:60-71`; `lib/TbAppLib/src/ToolChain.cpp:57-283`; `lib/TbUiLib/src/ToolBoxConnector.cpp:285-305,418-452`).

## Exact main-menu hierarchy

The following is the hierarchy built by `ActionManager::createMenu`, in source order. Horizontal rules represent source separators. The later keyboard appendix supplies the user-facing command, default binding, registry, and exact registration line for every leaf; contextual behavior is described in the analytical sections. **Open Recent** is a dynamic menu populated by the application rather than an `Action`; **Debug** and its eight leaves exist only when `NDEBUG` is not defined (`lib/TbUiLib/src/ActionManager.cpp:810-2054`; `lib/TbUiLib/src/ActionBuilder.cpp:96-159`).

- **File** (`ActionManager.cpp:823-984`)
  - New Document
  - —
  - Open Document…
  - Open Recent *(dynamic recent-document submenu)*
  - —
  - Save Document
  - Save Document as…
  - **Export**
    - Wavefront OBJ…
    - Map…
  - —
  - Load Point File…
  - Reload Point File
  - Unload Point File
  - —
  - Load Portal File…
  - Reload Portal File
  - Unload Portal File
  - —
  - Reload Material Collections
  - Reload Entity Definitions
  - —
  - Revert Document
  - Close Document
- **Edit** (`ActionManager.cpp:986-1244`)
  - Undo
  - Redo
  - —
  - Repeat Last Commands
  - Clear Repeatable Commands
  - —
  - Cut
  - Copy
  - Paste
  - Paste at Original Position
  - Duplicate
  - Delete
  - —
  - **Transform**
    - Flip Horizontally
    - Flip Vertically
    - Move…
  - **CSG**
    - Convex Merge
    - Subtract
    - Hollow
    - Intersect
  - **Vertices**
    - Snap Vertices to Integer
    - Snap Vertices to Grid
  - **Patches**
    - Convert Selection to Patches
  - **Materials**
    - Texture Lock *(checkable)*
    - UV Lock *(checkable)*
    - —
    - Replace Material…
- **Selection** (`ActionManager.cpp:1246-1330`)
  - Select All
  - Invert Selection
  - Deselect All
  - —
  - Select Siblings
  - Select Touching
  - Select Inside
  - Select Tall
  - Select by Line Number…
- **Groups** (`ActionManager.cpp:1332-1417`)
  - Group Selected Objects
  - Ungroup Selected Objects
  - Rename Selected Groups
  - —
  - Create Linked Duplicate
  - Select Linked Groups
  - Separate Selected Groups
  - Extract Selected Objects
  - Clear Protected Properties
- **Tools** (`ActionManager.cpp:1419-1581`)
  - Brush Tool *(checkable)*
  - Clip Tool *(checkable)*
  - Rotate Tool *(checkable)*
  - Sweep Tool *(checkable)*
  - Scale Tool *(checkable)*
  - Shear Tool *(checkable)*
  - Vertex Tool *(checkable)*
  - Edge Tool *(checkable)*
  - Face Tool *(checkable)*
  - Control Point Tool *(checkable)*
  - Deactivate Current Tool *(checked when no modal tool is active)*
- **View** (`ActionManager.cpp:1583-1928`)
  - **Grid**
    - Show Grid *(checkable)*
    - Snap to Grid *(checkable)*
    - Increase Grid Size
    - Decrease Grid Size
    - —
    - Set Grid Size 0.125 *(checked when current)*
    - Set Grid Size 0.25 *(checked when current)*
    - Set Grid Size 0.5 *(checked when current)*
    - Set Grid Size 1 *(checked when current)*
    - Set Grid Size 2 *(checked when current)*
    - Set Grid Size 4 *(checked when current)*
    - Set Grid Size 8 *(checked when current)*
    - Set Grid Size 16 *(checked when current)*
    - Set Grid Size 32 *(checked when current)*
    - Set Grid Size 64 *(checked when current)*
    - Set Grid Size 128 *(checked when current)*
    - Set Grid Size 256 *(checked when current)*
  - **Camera**
    - Move Camera to Next Point
    - Move Camera to Previous Point
    - Reset 2D Cameras
    - Focus Camera on Selection
    - Move Camera to…
  - —
  - Isolate Selection
  - Hide Selection
  - Show All
  - —
  - Show Map Inspector
  - Show Entity Inspector
  - Show Face Inspector
  - —
  - Toggle Toolbar *(checkable)*
  - Toggle Info Panel *(checkable)*
  - Toggle Inspector *(checkable)*
  - Maximize Current View *(checkable)*
  - —
  - Preferences…
- **Run** (`ActionManager.cpp:1930-1962`)
  - Compile Map…
  - Launch Engine…
  - —
  - Re-run compilation…
- **Debug** *(debug builds only; `ActionManager.cpp:1964-2033`)*
  - Print Vertices to Console
  - Create Brush…
  - Create Cube…
  - Crash…
  - Throw Exception During Command
  - Show Crash Report Dialog…
  - Set Window Size…
  - Show Palette…
- **Help** (`ActionManager.cpp:2035-2054`)
  - TrenchBroom Manual
  - About TrenchBroom

There is an intentional distinction between display position and persistent identity: Preferences is displayed under View but has ID `Menu/File/Preferences...`; About is displayed under Help but has ID `Menu/File/About TrenchBroom`; Selection, Groups, and Tools similarly retain older `Menu/Edit/...` IDs. The builder does not assign an explicit `QAction::MenuRole`, so Qt's default text heuristic may relocate Preferences and About into the macOS application menu (`ActionBuilder.cpp:64-92`). That platform behavior should not be mistaken for a different registry hierarchy.

## Application shell and persistent layout

The main window is a horizontal splitter containing the editing/info area and the Inspector. The editing area is a vertical splitter containing a switchable viewport container and the Info panel. Splitter positions, inspector visibility, info-panel visibility, and selected tabs are restored and saved (`lib/TbUiLib/src/MapWindow.cpp:419-486`; `lib/TbUiLib/src/Inspector.cpp:38-112`; `lib/TbUiLib/src/InfoPanel.cpp:35-85`).

The visible shell comprises:

| Region | User-facing contents |
|---|---|
| Menu bar | File, Edit, Selection, Groups, Tools, View, Run, Debug, Help. Debug is a real registered menu in this snapshot and should be treated as developer-facing/build-sensitive. |
| Main toolbar | Tool-mode buttons; duplicate/flip actions; texture and UV lock toggles; grid-size combo. It is fixed, non-floating, non-movable, with 24 px icons (`MapWindow.cpp:488-516`). |
| Map view bar | A context-sensitive tool page on the left and **View Options** popup on the right (`MapViewBar.cpp:47-71`). |
| Editing area | One-, two-, three-, or four-pane configurations of 3D, XY, XZ, and YZ viewports. Focus follows the pointer after a viewport is initially focused; a border indicates focus (manual `index.md:137-164`). |
| Inspector | Persisted tabs **Map**, **Entity**, and **Face** (`Inspector.cpp:38-62`). |
| Info panel | Persisted tabs **Console** and **Issues** (`InfoPanel.cpp:35-57`). |
| Status bar | Game, map format, current layer, open-group breadcrumb, detailed selection summary, hidden-object counts, and updater state (`MapWindow.cpp:526-757`). |

The layouts cycle as follows: one pane cycles 3D → XY → XZ → YZ; in two panes the right view cycles XY → XZ → YZ; in three panes the bottom-right cycles XZ ↔ YZ; four panes have no cycleable view (manual `index.md:143-160`). View preferences can synchronize 2D pan/zoom across orthographic views.

## Startup and global dialogs

### Welcome window

The fixed 700×500 Welcome window contains application/version information, a recent-documents list, **New map…**, **Browse…**, and file-open behavior. Double-clicking a recent document opens it. New map launches game/format selection; Browse uses an Open Map filter (`lib/TbUiLib/src/WelcomeWindow.cpp:39-160`; `lib/TbUiLib/src/AppInfoPanel.cpp:41-107`). The application information panel shows name, “Level Editor,” version, build, and Qt version; clicking version/build/Qt copies version information. An update indicator is also present.

### Game and map-format selection

The Game dialog is shown for new maps and for opened maps whose game cannot be identified. It presents a game list and, when applicable, map-format list, plus **Open preferences…**, OK, and Cancel. OK remains disabled until a game is selected, and double-click accepts. Missing game paths do not prohibit creating a map, but resource/model availability is reduced (`lib/TbUiLib/src/GameDialog.cpp:47-269`; manual `index.md:113-127`).

### About, crash, and update surfaces

- About combines `AppInfoPanel` with credits/license content (`lib/TbUiLib/src/AboutDialog.cpp:31-102`).
- Crash Report shows reason, version/build, report, map, and log information, with **Report** and **Close** (`lib/TbUiLib/src/CrashDialog.cpp:53-95`).
- The update dialog has explicit checking, available, up-to-date, downloading, preparing, ready-to-install, attention, and error pages. Depending on state, it exposes **Cancel**, **Download and install**, **Close**, **Install now**, **Install later**, or **Retry**, plus progress (`lib/UpdateLib/src/UpdateDialog.cpp:50-374`).
- Update indicators appear in Welcome, About, Update preferences, and the main status bar. Their visible states are Check for updates, Checking…, Update available, Up to date, Downloading…, Preparing…, Update pending, Update error, and Updates disabled (`lib/UpdateLib/src/UpdateIndicator.cpp:72-104`; manual `index.md:1640-1665`).

## Toolbar and map-view bar

The main toolbar order is source-defined in `ActionManager::createToolbar` (`lib/TbUiLib/src/ActionManager.cpp:2061-2099`):

1. Deactivate current tool / normal move mode.
2. Brush, Clip, Vertex, Edge, Face, Control Point, Rotate, Sweep, Scale, and Shear tools.
3. Duplicate, Flip Horizontally, Flip Vertically.
4. Texture Lock and UV Lock.
5. Grid-size combo, from 0.125 through 256 according to `mdl::Grid::MinSize..MaxSize` (`MapWindow.cpp:506-515`).

Buttons use the same Action objects as menus. Tool buttons and the two locks are checkable; their checked state mirrors the current tool or preference. Tool buttons are disabled when their tool cannot activate, such as Clip without brush geometry, Control Point without suitable patches, or Sweep without selected faces. The recurring enablement and checked-state policies are described below.

The map-view bar swaps its left-hand page according to the active tool and keeps View Options available on the right (`lib/TbUiLib/src/MapViewBar.cpp:47-71`; `lib/TbUiLib/src/MapViewToolBox.cpp:497-659`). This avoids opening a modal dialog for parameters that must remain visible during direct manipulation.

## View Options popup

`ViewPopupEditor` is a two-part popup (`lib/TbUiLib/src/ViewEditor.cpp:293-610,810-834`):

### Entity-definition visibility

A tree of entity-definition groups/classes provides checkboxes for filtering individual classes or whole groups, with **Show all** and **Hide all** controls. Every loaded class also receives a dynamic shortcut action `Entities/<classname>/Toggle`, labeled “Toggle <classname> visible.”

### Entities

- Show classnames.
- Show group bounds.
- Show group names.
- Show brush-entity bounds.
- Show point-entity bounds.
- Show point entities.
- Show point-entity models.

### Brushes and patches

- Show brushes.
- One dynamic **Show <smart-tag>** toggle per game-defined special brush/face tag.
- Show patches when the current map format supports patches.

### Renderer

- Face rendering radio group: **Show materials**, **Hide materials**, or **Hide faces**.
- **Shade faces**.
- **Use fog**.
- **Show edges**.
- Entity-link radio group: **All**, **Transitively selected**, **Directly selected**, or **Hidden**.
- **Show soft bounds**.
- **Restore Defaults**.

All options can receive shortcuts through Keyboard preferences. The renderer and visibility actions are checkable and query the active view's current state rather than maintaining duplicate UI state.

## Accelerator hosts and pointer-input precedence

TrenchBroom uses two accelerator hosts. Menu and toolbar commands live on shared window-level `QAction` objects. View-only and data-driven tag/entity actions become `QShortcut` children of each map view with `WidgetWithChildrenShortcut`; menu actions are deliberately excluded to prevent double activation. A view shortcut is enabled only while that viewport has focus and its action passes the current context and custom predicate. Ambiguous activations are logged (`lib/TbUiLib/src/MapViewBase.cpp:292-360`).

Pointer input passes through an ordered `ToolChain`. Pick, hover, modifier-change, scroll, and render notifications broadcast to every active controller. Clicks, double-clicks, drag acceptance, native-gesture acceptance, drop acceptance, and cancellation stop at the first controller that consumes them (`lib/TbAppLib/src/ToolChain.cpp:57-283`). The order therefore forms part of the editor's user-visible behavior:

- 3D: Camera → Move Objects → Rotate → Sweep → Scale → Shear → Extrude → Assemble Brush → Clip → Vertex → Edge → Face → Control Point → Entity Drop → Face-Attribute Transfer → Selection → Draw Shape (`lib/TbUiLib/src/MapView3D.cpp:109-133`).
- 2D: Camera → Move Objects → Rotate → Sweep → Scale → Shear → Extrude → Clip → Vertex → Edge → Face → Control Point → Entity Drop → Selection → Draw Shape (`lib/TbUiLib/src/MapView2D.cpp:127-148`).

This is how overlapping gestures remain deterministic. Ctrl/Cmd+left drag over already-selected geometry is claimed by Move Objects as duplicate-and-move; the same drag beginning over unselected geometry falls through to Selection paint-select. Camera navigation claims right/middle interactions before editing tools. Draw Shape is last and begins only when no earlier controller accepts the gesture. When drag threshold is crossed, `ToolBoxConnector` re-picks at the original mouse-down coordinate so the small threshold motion cannot change the intended handle (`ToolBoxConnector.cpp:362-372`).

## Viewport navigation, selection, and direct manipulation

### Camera and viewport navigation

| Input | 3D behavior | 2D behavior | Source/manual evidence |
|---|---|---|---|
| RMB drag | Mouse look | Pan | manual `index.md:190-225`; `CameraTool3D.*`, `CameraTool2D.*` |
| MMB drag | Strafe horizontally and move vertically | Pan | same |
| Alt+RMB drag | Orbit around the clicked point; wheel during orbit changes radius | — | manual `index.md:209-214` |
| Wheel | Move forward/back, optionally toward cursor | Zoom around coordinates under cursor | manual `index.md:194-207,221-225` |
| Shift+wheel | Temporary 3D camera zoom | — | manual `index.md:227-231` |
| RMB held + wheel | Adjust fly speed | — | manual `index.md:207`; Mouse preferences |
| Alt+Ctrl+wheel | Change grid size | Change grid size | manual `index.md:391-395` |
| W/S/A/D/Q/X | Fly forward/back/left/right/up/down | — | `Preferences.h:261-272` |

Camera menu actions additionally focus all views on the selection, move to an entered XYZ position, reset 2D cameras, reset temporary 3D zoom, move to the next/previous loaded point-file trace point, and cycle a cycleable viewport. Point files are green leak paths; portal files are translucent portal polygons (manual `index.md:2607-2611`).

### Selection semantics

| Input/action | Result |
|---|---|
| LMB | Replace object selection. In 3D, picks the frontmost candidate; in 2D, chooses the candidate face/object with the smallest visible area. |
| Ctrl+LMB | Toggle/add an object. |
| Ctrl+LMB drag starting off selected geometry | Paint-select objects, including occluded candidates. |
| Ctrl+wheel over geometry | Drill 3D selection away from or toward the camera. |
| Shift+LMB in 3D | Select one brush face. |
| Ctrl+Shift+LMB | Add/toggle brush faces. |
| Shift+double-LMB | Select all faces of a brush. |
| Shift+Alt+double-LMB | Flood-select a touching coplanar surface; Ctrl adds to the existing face selection. |
| Ctrl+Shift+drag | Paint-select faces. |
| Double-LMB on an object inside an entity/group | Select siblings, or open a group for editing; double-click outside closes the open group. |
| Click void / Deselect All | Clear selection. |

The menu adds Select All, Invert, Siblings, Touching, Inside, Tall, and Select by Line Number. Touching/Inside consume selected “selection brushes”; Tall projects them onto the focused 2D view as a lasso-like prism. Select by Line Number accepts comma- or space-separated positive map-file lines (`lib/TbUiLib/src/MapWindow.cpp:1543-1598`; manual `index.md:241-269`).

### Moving, duplicating, pasting, and material transfer

- Drag a selected object to move all selected objects. 3D defaults to XY; hold Alt for Z. 2D uses its view plane. Shift can be pressed during a drag to restrict motion to the dominant axis. The move trace becomes thicker when restricted (`index.md:373-389,595-614`).
- Ctrl at drag start duplicates and moves. Directional duplicate commands perform the same operation by one grid step (`index.md:558-579`).
- Paste positions objects under the cursor/on hit geometry and snaps their bounds; Paste at Original Position preserves coordinates (`index.md:581-589`).
- Material/attribute transfer starts from a selected source face. Alt+click/drag/double-click projects material and attributes to one face, a painted chain, or a whole brush. Alt+Shift rotates the UV axes onto targets in Valve format. Alt+Ctrl transfers only the material and preserves target attributes. Content flags are always preserved (`index.md:1010-1037`; `SetBrushFaceAttributesTool.*`).

## Map-view context menu

The popup is assembled from current selection and the object beneath the cursor, so its contents and labels are contextual (`lib/TbUiLib/src/MapViewBase.cpp:1186-1504`):

- Group, Ungroup, Merge Groups [into the hovered/eligible group], Rename Groups.
- When applicable, **Add Objects to Group <name>** and **Remove Objects from Group <name>**.
- Create Linked Duplicate, Select Linked Groups, Separate Linked Groups, Extract Linked Groups.
- **Move to Layer** submenu containing every layer, each independently enabled through `canMoveSelectedNodesToLayer`.
- **Make Layer <name> Active**, or a dynamic **Make Layer Active** submenu if selection spans multiple layers.
- Hide Layers, Isolate Layers, Select All in Layers.
- For geometry: Make Structural; when an eligible target entity is beneath the cursor, **Move to Entity <name>**.
- For an entity hit: **Select All <classname>**.
- For a face hit: **Reveal <material> in Material Browser** and **Copy Material Name**.
- Dynamic **Create Point Entity** and **Create Brush Entity** submenus, grouped by definition category. Worldspawn is excluded; brush-entity entries are disabled unless the current selection can form a brush entity.

The popup is queued until after paint-time input handling, then the editor synthesizes a mouse-move event after it closes so hover/pick state is immediately correct (`MapViewBase.cpp:1186-1462`).

### Other popup and local context menus

These surfaces are constructed locally rather than from the central action registry, so most have no stable action ID or configurable shortcut:

| Surface | Complete local menu / popup contents and behavior | Source |
|---|---|---|
| Open Recent | One action per filtered, still-valid recent path, labeled with the filename; selecting it loads that path. The submenu contains no separate Clear item in this revision. | `RecentDocuments.cpp:85-193` |
| Layers | Make active; Move selection to layer; Select all in layer; Show/Hide; Isolate; Lock/Unlock; Omit From Export; Show All; Hide All; Unlock All; Lock All; Rename; Remove. | `LayerEditor.cpp:78-134` |
| Material tile | Select Faces; Select Brushes; Copy Name. | `MaterialBrowserView.cpp:472-495` |
| Issues | Show; Hide; Fix submenu containing only quick fixes common to all selected issues. | `IssueBrowserView.cpp:299-354` |
| Console / compilation output | Standard read-only text menu plus separator and Clear. | `Console.cpp:130-136`; `CompilationDialog.cpp:294-300` |
| Compilation profile | Duplicate; Remove. | `CompilationProfileManager.cpp:202-209` |
| Compilation task | Move Up/Down when possible; Duplicate; Remove. The Add button opens Export Map, Copy Files, Rename File, Delete Files, Run Tool, and Launch Engine. | `CompilationProfileEditor.cpp:139-153,204-252` |
| Entity-property defaults | Set existing default properties; Set missing default properties; Set all default properties. | `EntityPropertyGrid.cpp:294-306` |
| Smart-tag mutation choice | When a game-defined tag operation offers multiple concrete values, an action popup at the cursor lists every option; dismissal returns no selected option. The surrounding mutation is an undo transaction. | `EnableDisableTagCallback.cpp:29-43`; `MapViewBase.cpp:740-759` |
| Draw Shape selector | Cuboid; Stairs; Arch; Cylinder; Cone; Spheroid (UV); Spheroid (Icosahedron). Choosing one swaps the matching parameter page. | `DrawShapeToolPage.cpp:55-104`; `DrawShapeToolExtensionKind.h:34-53` |

This distinction matters for Mason's command design. Local menus are quick to build, but without registered identities they cannot participate automatically in keymap editing, documentation, telemetry, command search, or automation.

## Editing tools and their contextual controls

The tool box contains permanent tools (camera, selection, shape drawing, entity drag/drop, resize/extrude, move) and modal tools. Modal tools are mutually exclusive, with explicit suppression groups in `MapViewToolBox.cpp:497-588`; some can temporarily combine, such as rotating/scaling/shearing vertex-tool handles. Escape first cancels the current operation/state and then deactivates; Shift+Escape directly deactivates the current modal tool (`manual index.md:321-371`).

### Tool inventory

| Tool | Activation and prerequisites | Contextual bar | Viewport interaction and commit |
|---|---|---|---|
| Normal move | Default/deactivate current tool | No special page | Drag selected objects; Alt selects 3D vertical movement; Shift axis-locks; Ctrl at start duplicates. Arrow/Page keys move by grid. |
| Draw Shape | Permanent when compatible | Shape selector and **Group** checkbox | Drag bounds in 2D/3D. In 3D, Shift constrains X=Y, Shift+Alt constrains X=Y=Z, Alt changes height. Shape page parameters update preview; Apply commits parameters. |
| Assemble Brush | **Brush Tool** / B; 3D only | Empty page | Click a point on a brush face; double-click adds all face vertices; drag a rectangle on a face adds four corners; Shift-drag a polygon duplicates it along its normal; Return creates the convex hull. |
| Clip | **Clip Tool** / C; brush selection | Empty page | Click to place 2–3 points, drag to place two, drag an existing point to move, double-click a 3D face to match its exact plane. Ctrl+Return cycles keep-front/keep-both/keep-back; Return clips; Delete removes latest point; Escape unwinds points/tool. |
| Rotate | **Rotate Tool** / R; transformable selection/handles | Recent/editable center, Reset, angle, X/Y/Z, Apply, **Update entity properties** | Drag colored rings (3D axes or 2D normal) and center handle. Keyboard roll/yaw/pitch uses configured angle and center while active, otherwise 90° around snapped selection center. |
| Sweep | **Sweep Tool** / Y; selected brush faces | Segments 1–64, Path Arc/Straight/S-bend, Iterations 1–8, **Snap to integer grid**, Reset | Center moves destination cap; rings rotate; green handle scales; movement/rotation/`[`/`]` keys adjust; Return creates the brush run; Escape resets then exits. |
| Scale | **Scale Tool** / T; transformable selection/handles | Two modes: desired XYZ size or XYZ factor; Apply | 3D sides scale one axis, edges scale two proportionally, corners all three; 2D sides one axis/corners two. Shift proportional; Alt centers anchor. |
| Shear | **Shear Tool** / G | Empty page | Drag selection-bounds side; Alt enables vertical 3D motion except on top/bottom. Alignment lock works for Valve 220 only. |
| Vertex | **Vertex Tool** / V; selected brushes | Empty page | Click/Ctrl-click handles; drag empty area for rectangle selection, Ctrl forces select; drag selected vertices; Ctrl during move toggles relative vs absolute grid snap; Shift+Alt-click snaps selected vertex to target; Shift reveals/drag-adds new vertex; Delete removes valid selected handles. Shared positions are clumped across brushes. |
| Edge | **Edge Tool** / E; selected brushes | Empty page | Same handle selection/lasso/move and keyboard movement as Vertex; relative snapping only; shared edges can move together. |
| Face | **Face Tool** / F; selected brushes | Empty page | Same handle selection/lasso/move and keyboard movement as Vertex; relative snapping only; back-facing face-center handles remain selectable. |
| Control Point | **Control Point Tool** / P; patch-capable map and selected patches | Odd Rows and Columns, minimum 3, step 2 | Select/lasso/drag clumped patch control points. Changing dimensions resamples; increasing can preserve shape exactly, decreasing approximates. |
| Extrude/resize | Permanent when no suppressing modal tool | No page | Shift+drag highlighted selected face extrudes; Ctrl also splits off a new brush; Ctrl+Alt stamps; Shift+Alt moves a face. Multiple exactly coincident faces may extrude together. |
| Entity drag | Permanent | Entity Browser | Drag an entity definition into 2D/3D view; placement follows hit geometry/selection depth and snaps its bounds. |

Sources: manual `index.md:447-946`; `lib/TbUiLib/src/MapViewToolBox.cpp:497-659`; `DrawShapeToolPage.cpp:55-104`; `DrawShapeToolExtensionPages.cpp:79-423`; `RotateToolPage.cpp:110-240`; `SweepToolPage.cpp:50-164`; `ScaleToolPage.cpp:75-157`; `ControlPointToolPage.cpp:66-146`; corresponding `lib/TbAppLib/src/*ToolController*.cpp`.

### Shape catalog and parameters

The Shape selector exposes Cuboid, Stairs, Arch, Cylinder, Cone, Spheroid (UV), and Spheroid (Icosahedron) (`lib/TbAppLib/include/ui/DrawShapeToolExtensionKind.h:34-53`; `lib/TbAppLib/src/DrawShapeToolExtensions.cpp:59-355`). Controls are:

- Circular common controls: axis X/Y/Z; 3–96 sides; Edge-aligned, Vertex-aligned, or Scalable circle. Scalable curves constrain precision to 12/24/48/96 sides.
- Cylinder: Hollow and thickness 1–128.
- Cone: common circular controls.
- Icosphere: accuracy 1–4.
- UV sphere: rings 1–256 except in scalable mode.
- Stairs: step height 1–1024 and direction +X/−X/+Y/−Y.
- Arch: Spandrel and thickness 1–1024, plus circular controls.
- Group checkbox controls whether multibrush results become a group. Cuboid needs no parameter page.

`DrawShapeToolExtensionPages.cpp:79-423` is the canonical source; the manual explains scalable CZG curves and cross-shape parameter compatibility at `index.md:447-492`.

### CSG, patch conversion, and repeatable transformations

The Edit menu supplies Convex Merge, Subtract, Hollow, and Intersect. Convex Merge builds one convex hull; Subtract cuts all selectable/visible minuends with selected subtrahends; Hollow creates walls at current grid thickness; Intersect keeps the common volume. Material inheritance first searches an input face on the same plane, otherwise uses the current material (`manual index.md:905-950`). Convert Selection to Patches replaces each selected brush face with one or more quad patches and is enabled only in a patch-capable map.

Repeat and Clear Repeatable Commands form a small macro mechanism. Repeatable commands recorded after the last selection change/clear are replayed as a sequence, supporting patterns such as duplicate → move → rotate for stairs (`manual index.md:1667-1675`).

## Inspector inventory

### Map Inspector

The Map tab contains a persistent **Layers** panel and collapsible, state-persisted **Map Properties** and **Mods** panels (`lib/TbUiLib/src/MapInspector.cpp:85-161`).

#### Layer editor

Each map has a non-removable Default Layer and any number of user layers. A layer row conveys current state (radio/bold), omit-from-export, visibility, and lock state. Double-click makes a layer current. The row icons directly toggle omit, eye/visibility, and lock. The bottom toolbar adds, removes, moves up, and moves down; removing reparents children into Default Layer and switches the current layer if necessary (`LayerEditor.cpp:224-337,420-480`; manual `index.md:1513-1547`).

The layer context menu is complete and state-checked (`LayerEditor.cpp:78-134`):

- Make active layer.
- Move selection to layer.
- Select all in layer.
- Show/Hide layer.
- Isolate layer.
- Lock/Unlock layer.
- Checkable Omit From Export.
- Show All Layers / Hide All Layers.
- Unlock All Layers / Lock All Layers.
- Rename Layer.
- Remove Layer.

The Default Layer cannot be renamed or removed. Removal is also prevented when no other visible, unlocked destination layer can keep the editor in a valid state (`LayerEditor.cpp:256-317,396-417`). Export Map omits all marked layers; newly created/pasted objects enter the current layer unless working inside an open group.

#### Map Properties

Soft bounds have three radio modes: **Soft bounds disabled**, **Use game default** with visible Min/Max, and **Use custom bounds** with editable Min/Max. A field accepts a scalar or vector; custom bounds commit only when both parse and `min < max` (`MapInspector.cpp:173-313`). These are editor guides only and do not alter engine limits.

#### Mods

The panel has **Available** and **Enabled** lists, extended selection, an available-mod search, and buttons to enable, disable, move up, and move down. Double-click moves an item between lists. Order is resource priority; the game's base/default mod is omitted from Available. The enabled list persists through worldspawn `_tb_mod` (`ModEditor.cpp:95-190,225-280`; manual `index.md:279-287`).

### Entity Inspector

The Entity tab uses a vertical splitter: Entity Property Editor above and a switchable **Browser / Settings** panel below (`lib/TbUiLib/src/EntityInspector.cpp:46-95`).

#### Entity Property Editor

The property grid has **Protected**, **Key**, and **Value** columns. The Protected column appears for entities inside linked groups. Multiple selections show the union of keys: keys missing from some entities are gray, and differing values show blank. Editing applies the property to all selected entities (`EntityPropertyEditor.cpp:82-278`; `EntityPropertyGrid.cpp:233-388`; manual `index.md:1142-1180`).

Bottom controls are:

- `+`: add a property; Ctrl+Return is the keyboard equivalent.
- Shielded `+`: add a property already protected from linked-group propagation.
- `−`: remove selected properties.
- Default-properties popup: **Set existing default properties**, **Set missing default properties**, **Set all default properties**.
- **Show default properties** checkbox; defaults appear italic until instantiated.

Tab/Shift+Tab moves across key/value cells; Return moves vertically. Clicking an already selected cell enters an editor. Protecting a property prevents changes from flowing to or from linked siblings; deleted protected properties act as suppression markers. **Clear Protected Properties** removes all such overrides (`manual index.md:1150-1190,1320-1391`).

The area beneath the grid switches among documentation and smart editors based on the chosen property:

| Editor | Behavior |
|---|---|
| Choice | Editable combo of definition-provided choices; arbitrary text remains possible (`SmartChoiceEditor.cpp`). |
| Flags/spawnflags | Named bit checkboxes with combined integer value (`SmartFlagsEditor.cpp`, `FlagsEditor.cpp`). |
| Color | Color chooser, byte `[0,255]` versus float `[0,1]` conversion, and colors found in the map (`SmartColorEditor.cpp`). |
| WAD | Add/remove/reorder WAD paths and reload; the worldspawn `wad` property is edited through this UI. External WAD files can also be dropped on the editor (`SmartWadEditor.cpp`; manual `index.md:301-307`). |
| Default | Plain property-value editing for other types (`SmartDefaultPropertyEditor.cpp`). |

Entity-class and property documentation plus flag/choice descriptions appear when definitions supply them; empty documentation/editor panels are hidden (`EntityPropertyEditor.cpp:230-278`).

#### Entity Browser

The visual tile browser supports **Name / Usage** sorting, **Group** by classname prefix/category, **Used** only, and name search. Tiles can be dragged into 2D or 3D viewports. A drop in 3D rests the snapped entity bounds on hit geometry; a 2D drop derives depth from the most recent selection (`EntityBrowser.cpp:66-132`; `EntityBrowserView.cpp`; manual `index.md:530-548`). The map context menu and dynamically assignable **Create <classname>** shortcuts provide two additional creation paths.

#### Entity-definition Settings

Settings lists built-in definition files and offers an external-file path with **Browse** and **Reload**. Supported sources are Valve FGD, Radiant DEF, and ENT. Built-ins select immediately; external definitions persist through worldspawn `_tb_def`; Reload also refreshes referenced models (`EntityDefinitionFileChooser.cpp:106-269`; manual `index.md:289-297`).

### Face Inspector

The Face tab puts the UV Editor and Face Attributes above a switchable material **Browser / Settings** panel (`lib/TbUiLib/src/FaceInspector.cpp:84-167`).

#### UV Editor

The UV editor is active only for exactly one face. It shows the tiled material, gray UV grid, white face outline, yellow origin, red origin axes, a rotation ring, and UV axes (`UvEditor.cpp:55-195`; manual `index.md:1114-1140`).

- Drag background/material to change offset; it snaps to face vertices.
- Drag gray grid lines to scale; they snap to vertices.
- Drag yellow origin for both axes, or a red axis for one coordinate; origin snaps to face vertices and center.
- Drag the large yellow ring, or Ctrl-drag anywhere, to rotate; angles snap to face edges.
- Alt-drag gray grid lines to shear in parallel/Valve projection maps.
- Toolbar: reset alignment, reset to world/paraxial alignment, flip U, flip V, rotate −90°, rotate +90°, and independent U/V grid subdivision from 1 to 16.

#### Face Attributes and alignment buttons

The editor contains Material name, texture Size, X/Y Offset, X/Y Scale, Angle, and game-format-dependent Value, Surface Flags, Content Flags, and Color. Multi-face disagreements display `multi`; unavailable attributes display `n/a`; unset buttons restore optional values. Flag fields open checkbox popups (`FaceAttribsEditor.cpp:363-618`).

Spin deltas are context-aware: Offset uses grid size, Shift uses twice the grid, Ctrl uses 1; Scale uses 0.1, Shift 0.25, Ctrl 0.01; Angle uses 15°, Shift 90°, Ctrl 1°. Mouse wheel and arrow keys in focused numeric fields use the same deltas (`manual index.md:1070-1094`).

Seven fast alignment buttons surround Auto Fit:

- Align texture with a face edge; repeated clicks cycle edges, Shift reverses.
- Justify Up/Down/Left/Right; repeated clicks cycle atlas positions, Shift reverses.
- Fit Horizontally/Vertically; repeated clicks increase integer repeat factors, Shift reverses. Ctrl cycles fractional trim-sheet subdivisions, and Ctrl+Shift reverses.
- Auto Fit aligns, justifies, and fits.

#### Material Browser and Settings

The material tile browser supports **Name / Usage** sorting, **Group** by collection, **Used** only, and multi-word search (`MaterialBrowser.cpp:109-173`). Used materials have a yellow border; current material has red. Clicking applies to selected faces/all faces of selected brushes and also sets current material. Context commands are **Select Faces**, **Select Brushes**, and **Copy Name**; Viewport context can reveal a hit material (`MaterialBrowserView.cpp:472-495`; manual `index.md:982-996`).

Settings shows **Available** and **Enabled** material collections. Add/remove/reload buttons and double-click move collections; order determines conflict priority. WAD-based games instead direct the mapper to the worldspawn WAD editor (`MaterialCollectionEditor.cpp:126-217`).

## Groups, linked groups, and visibility organization

Regular groups select and transform as a unit. Group creates a named group from selected objects; Ungroup preserves contents. Double-click opens a group and locks everything outside it; double-click outside closes it. Context commands add/remove objects, merge groups, and rename them (`manual index.md:1255-1262`).

Linked groups are synchronized structural copies with independent group transforms. Commands create a linked duplicate, select linked siblings, separate selected members into a new link set, or extract a subset of objects from an open linked group into new mutually linked groups. Any opened member can be edited; its resulting contents propagate through sibling transforms. Protected entity properties provide intentional per-instance overrides (`manual index.md:1263-1439`). Viewport rendering uses a separate linked-group color and arrows from a selected/open member to siblings.

Hide, Isolate, and Show All are undoable. Locked layers and objects outside an open group cannot be selected or edited. View filtering differs from hiding: filtering is a renderer/editor-category toggle driven by entity definitions and smart tags; hiding is map editing state and contributes to status counts (`manual index.md:1227-1253`).

## Info panel, issue validation, and console

### Console

Console is a read-only, no-wrap, fixed-width log viewer with severity colors. It periodically drains cached messages and provides the standard text context menu plus **Clear** (`lib/TbUiLib/src/Console.cpp:67-136`). The same text-output behavior is reused in compilation output.

### Issue Browser controls and behavior

The Issues tab has **Show hidden issues**, a dynamic **Filter** popup of registered validators, and a Line/Description table. Validation is delayed by 500 ms after edits so continuous interaction is not blocked. Selecting issue rows selects their affected map nodes/faces. Hidden issues are italic. Right-click exposes **Hide** or **Show** and a **Fix** submenu containing quick fixes common to the selected issues (`lib/TbUiLib/src/IssueBrowser.cpp:37-120`; `IssueBrowserView.cpp:85-389`).

The pinned snapshot registers these 20 validator categories in `mdl::Map::registerValidators` (`lib/TbMdlLib/src/Map.cpp:1093-1120`):

| Validator shown in Filter | Condition | Available quick fix(es) |
|---|---|---|
| Missing entity classname | Entity lacks `classname` | Delete Objects |
| Missing entity definition | Class has no loaded definition | Delete Objects |
| Missing mod directory | Enabled mod path does not exist | Remove Mod |
| Empty group | Group has no children | Delete Objects |
| Empty brush entity | Brush entity has no geometry | Delete Objects |
| Point entity with brushes | Point-class entity contains brush children | Move Brushes to World |
| Missing entity link source | Configured source link is invalid/missing | Delete Property |
| Missing entity link target | Link names a nonexistent target | Delete Property |
| Non-integer vertices | Brush has fractional vertices | Snap Vertices to integer grid |
| Mixed brush content flags | Faces of a brush disagree on content flags | No automatic fix in this snapshot |
| Objects out of world bounds | Node exceeds hard world bounds | Delete Objects |
| Objects out of soft map bounds | Node exceeds configured guide bounds | Delete Objects |
| Empty property name | Entity key is empty | Delete Property |
| Empty property value | Entity value is empty | Delete Property |
| Long entity property keys | Key exceeds game maximum | Delete Property |
| Long entity property value | Value reaches/exceeds game maximum | Delete Property; Truncate Property Values |
| Invalid entity property keys | Key contains `"` | Delete Property; Replace `"` with `'` |
| Invalid entity property values | Value contains `"` | Delete Property; Replace `"` with `'` |
| Paths must use forward slashes | Worldspawn WAD/definition paths contain `\` | Replace `\` with `/` |
| Invalid UV scale | Face UV scale is invalid | Reset UV Scale to 1,1 |

Validator names and fixes come from `lib/TbMdlLib/src/*Validator.cpp` and `IssueQuickFix.cpp:69-125`. For Cypher, validation types should be enumerable from the registry so the Issues UI cannot drift from the active checks.

## Preferences: every pane and exposed setting

The Preferences dialog has **Games, View, Colors, Mouse, Keyboard, Update** panes in this revision. It always offers **Restore Defaults**. On macOS-like instant-save behavior, changes apply immediately; on staged platforms it exposes OK, Apply, and Cancel and prompts **Save / Discard / Cancel** when closing with pending changes (`lib/TbUiLib/src/PreferenceDialog.cpp:61-215`).

### Games

- Installed/configured game list.
- Open custom game-configurations folder.
- Per-game Game Path text field with validation/error styling and folder Browse.
- **Configure engines…**.
- One validated file path per game-defined compilation tool, including source-provided tooltip (`GamesPreferencePane.cpp:69-325`).

### View

- Theme: **System** or **Dark**; changing theme requires restart.
- View layout: One, Two, Three, or Four panes.
- **Sync 2D views**.
- Brightness.
- Grid opacity.
- 3D field of view, 50–150 degrees.
- Show coordinate axes.
- Texture filter: Nearest; Nearest mipmapped; Nearest with mip interpolation; Linear; Linear mipmapped; Trilinear.
- Enable multisampling.
- Material-browser icon size, 25–300%.
- Renderer font size, validated 1–96 with common presets (`ViewPreferencePane.cpp:107-220`).

### Colors

A searchable Color/Context/Description table is generated from the central color-preference registry. Double-click opens a non-native picker. Alpha is preserved even though the table swatch displays opaque color; read-only axis/compass colors are omitted; Reset restores all editable color defaults (`ColorsPreferencePane.cpp:37-113`; `ColorModel.cpp:42-189`).

The registry contains 58 color preferences, 52 editable here after six read-only axis/compass entries are removed. Editable categories include soft bounds; background; point/portal files; camera frustum; regular and linked groups; tutorial overlays; normal/selected/locked faces and edges; undefined entities; selection bounds; all info overlays; normal/occluded/selected handles; clip; resize/extrude; rotate; scale; shear; move traces/indicators; angle indicator; texture seam; 2D grid; browser text/subtext/group/background; and material-browser default/selected/used colors (`lib/TbPreferencesLib/src/Preferences.cpp:41-102`; `Preferences.h:52-231`).

### Mouse

- Mouse Look: sensitivity, invert X, invert Y.
- Mouse Pan: sensitivity, invert X, invert Y.
- Mouse Move: sensitivity, invert wheel, enable Alt+MMB tablet movement, invert the Alt-move zoom direction, move camera toward cursor.
- Fly Mode speed. While RMB is held, the wheel changes this speed interactively (`MousePreferencePane.cpp:43-105`; `Preferences.h:233-259`).

### Keyboard

Searchable Description/Context/Shortcut/Alternative table for all static menu and viewport actions, six fly keys, and—when a document is open—every loaded smart-tag and entity-definition action. Clicking twice enters capture without interpreting the sequence as an editor command. Conflicts are context-aware, sorted/colored, and block acceptance. **Reset all** restores defaults (`KeyboardPreferencePane.cpp:45-148`; `KeyboardShortcutModel.cpp:260-378`).

### Update

- Check for updates on startup.
- Include pre-releases.
- Developer/transient draft-release controls when enabled by the build/configuration.
- Embedded update status/action indicator (`UpdatePreferencePane.cpp:46-159`; `Preferences.h:35-44`).

## Dialog and popup inventory

| Surface | Fields, buttons, and semantics | Source |
|---|---|---|
| Save / Save As / Revert | Standard document flows. Save As uses map filters; closing a modified document asks whether to save. Revert uses a destructive **Revert** plus Cancel. | `MapWindow.cpp:970-1185` |
| Load Point File | File picker for compiler-generated point path; Reload and Unload become enabled only after a file is loaded. | `MapWindow.cpp:1187-1234`; ActionManager File menu |
| Load Portal File | File picker for portal file; Reload and Unload are state-dependent. | `MapWindow.cpp:1235-1275`; ActionManager File menu |
| Export Wavefront OBJ | Output path + Browse; material texture paths relative to game path or export path; Export/Cancel. | `ObjExportDialog.cpp:61-153` |
| Export Map | Save-file picker; produces `.map` while omitting marked layers. | `MapWindow.cpp:1070-1118` |
| Replace Material | Two material browsers, Find side defaulting to Used-only, replacement side, Replace button enabled only after both choices; scope is selected faces or whole map; completion/failure message. | `ReplaceMaterialDialog.cpp:77-211` |
| Move Objects | XYZ vector text; parse failure reports the invalid value. | `MapWindow.cpp:1720-1748` |
| Move Camera | XYZ position text; affects 3D camera, not 2D views. | `MapWindow.cpp:1986-2003` |
| Select by Line Numbers | Comma- or space-separated positive lines; selects nodes with matching file positions. | `MapWindow.cpp:1575-1598` |
| Rename Group / Layer | Text prompt; group names need not be unique; Default Layer cannot rename. | `ViewUtils.cpp:90-145`; `LayerEditor.cpp:298-317` |
| Choose Path Type | Radio choices: Absolute; relative to map; relative to application executable; relative to game directory. Unavailable bases disable choices; each shows the resulting preview. | `ChoosePathTypeDialog.cpp:59-173` |
| Game Engines | Current-game indicator; **Profiles** list; add/remove; **Details** Name and executable Path + Browse; Close saves configuration. | `GameEngineDialog.cpp:38-89`; `GameEngineProfileManager.cpp:40-137`; `GameEngineProfileEditor.cpp:45-177` |
| Launch Engine | Current game; engine profiles; **Configure engines…**; variable-completing Parameters; Launch/Close. Profile selection fills/stores parameters; double-click/Return launches. | `LaunchGameEngineDialog.cpp:60-272` |
| Compile | Profile manager, task editor, Output, Launch…, Stop, Test, Compile, Close, current-run status. Compile/Test require idle selected profile with tasks; Stop requires running; closing/Escape while running asks before termination. | `CompilationDialog.cpp:89-310` |
| About | Application identity, version/build/Qt, credits and licenses, update state. | `AboutDialog.cpp:31-102`; `AppInfoPanel.cpp:41-107` |
| Crash Report | Reason, environment/version, crash report, map/log context; Report/Close. | `CrashDialog.cpp:53-95` |
| Update | Stateful pages and controls described above. | `lib/UpdateLib/src/UpdateDialog.cpp:50-374` |

Developer-only Debug dialogs also exist for Create Brush from a point list, Create Cube from a size, choose crash type, set window W/H, and display a palette. They are registered in the Debug menu and should not be treated as mapper workflow (`MapWindow.cpp:2250-2397`; `ActionManager.cpp:1964-2034`).

## Compilation and launch workflow

The Compile dialog's left side manages profiles; the right side edits **Name**, variable-aware **Working Directory**, and an ordered task list. Profile context menu is Duplicate/Remove. Task toolbar and context menu provide add, remove, move up/down, and duplicate, with boundary-aware enablement (`CompilationProfileManager.cpp:40-220`; `CompilationProfileEditor.cpp:72-270`). Every task has an Enabled checkbox.

The add-task popup and editor fields are exhaustive (`CompilationProfileEditor.cpp:204-252`; `CompilationTaskListBox.cpp:128-728`):

| Task | Fields and behavior |
|---|---|
| Export Map | File Path; Strip Entities GLOB; Add Entity classname at camera origin/direction; Strip TrenchBroom-specific `_tb_` properties. Omit-from-export layers are excluded. |
| Copy Files | Source File Path supporting `*`/`?`; Target Directory Path created recursively; overwrite without prompt. |
| Rename File | Source File Path without wildcards; Target File Path; create parent directories and overwrite. |
| Delete Files | File Path supporting `*`/`?`. |
| Run Tool | Tool Path + Browse; Parameters; Stop on nonzero error code. Captures output. |
| Launch Engine | Engine Profile combo; Stop on launch failure. |

Working-directory and task fields provide `${...}` completion. Core variables are `WORK_DIR_PATH`, `MAP_DIR_PATH`, `MAP_BASE_NAME`, `MAP_FULL_NAME`, `GAME_DIR_PATH`, `MODS`, `APP_DIR_PATH`, and `CPU_COUNT`, plus game-defined compiler-tool variables. **Test** prints intended operations without mutating/running; **Compile** runs enabled tasks sequentially in the background; **Stop** terminates the active tool. Manual engine launch provides `MAP_BASE_NAME`, `GAME_DIR_PATH`, and `MODS` and stores Parameters on the selected engine profile (`manual index.md:1689-1845`).

## Status, feedback, and discoverability

The status bar builds a structured line rather than a single selection count (`MapWindow.cpp:568-757`):

1. Current game.
2. Map format.
3. Current layer.
4. Open-group breadcrumb from outermost to innermost.
5. Counts of selected brushes, patches, faces, entities, and groups; common containing entity classname or “multiple entities”; common entity classname or “multiple classnames”; one selected layer name or number of layers.
6. Counts of hidden groups, entities, brushes, and patches.
7. Update state/action at the far side.

Additional feedback is placed at the point of action: tool handles highlight red/yellow, move traces show axis locks, selected/locked edges use distinct colors, group bounds/names can be toggled, current/used materials use red/yellow borders, invalid preference paths use error styling, shortcut conflicts appear red, and updater/compile states replace available controls rather than allowing invalid clicks.

One source-level anomaly is worth preserving in review notes: the outer hidden-count condition at `MapWindow.cpp:723` tests groups/entities/brushes but not patches, even though patches are appended at lines 739-742. If only patches are hidden, the status text may omit them. This is an observed implementation detail, not an intended capability.

## Enablement and checked-state model

The preceding command analysis describes action-specific behavior, and the
recurring enablement and checked-state policies are:

| Command family | Typical enable precondition | Typical checked-state source |
|---|---|---|
| Document | A document exists; Save/Revert additionally reflect modified/path state; reload/unload resource actions require a loaded resource. | N/A |
| Undo/Redo | Corresponding command stack entry exists. Label changes to `Undo <command>` / `Redo <command>`. | N/A |
| Clipboard | Selection is copyable/cuttable; paste accepts parseable map/face data. | N/A |
| Selection | Current selection and focused viewport satisfy operation: Tall requires focused 2D; Touching/Inside require brush selection; Siblings requires eligible hierarchy. | N/A |
| Transform/CSG | Transformable nodes or tool-owned handles, with no incompatible modal tool; CSG validates brush/handle geometry. | N/A |
| Tool toggle | Tool-specific `canActivate`; invoking active tool deactivates it. | Exact active tool in `MapViewToolBox`. |
| Texture/UV lock | Document exists. | `Preferences::AlignmentLock` / `Preferences::UvLock`. |
| Grid | Document exists; increase/decrease stop at min/max. | Show Grid and Snap to Grid query map grid; exact size actions compare active size. |
| Render/filter | Document and active view exist. | Active view filter/render state. |
| Inspector/info/toolbar | Document/window exists. | Widget visibility or active page. |
| Compile/launch/rerun | Document exists; rerun also needs a previous run. | N/A |

Menus and toolbar therefore do not each reimplement state logic. `ActionBuilder` copies static checkability/icon/status text, while `MapWindow::updateActionState` calls the current predicates after document/view/tool changes (`ActionBuilder.cpp:31-70`; `MapWindow.cpp:343-397`). Dynamic layer, material, issue, and map-view popup actions use the same pattern locally: construct, call a domain `can...` query, set check state, then execute through the domain operation.

This is a strong model for Cypher. Every editor mutation should expose a query and an execution path over the same state, and a UI action should remain a thin adapter. The query must be safe to call frequently and must explain disabled state when surfaced in a command palette or tooltip.

## What Mason and TileEditor should implement from this research

### Shared editor kernel

Mason and TileEditor should share the following subsystems rather than develop parallel ad-hoc UIs:

1. **Command registry.** Stable ID; localized label; category/menu placement; default and alternate bindings; applicable editor/view/tool/selection contexts; enable/check predicates; execution; undo/repeat metadata; icon; short and long help.
2. **Input router.** Focused viewport, modifier/button state, pick result, modal drag transaction, cancellation, and ordered tool-controller chain. It must resolve the same key differently by context without hidden priority accidents.
3. **Transaction/undo service.** Begin/update/commit/cancel around every drag; collation for repeated key steps; composite commands for operations such as deselect+hide; repeatable-command recording.
4. **Selection service.** Typed selection sets, additive/toggle selection, paint/lasso selection, cycling/drilling, hierarchy expansion, lock/filter exclusion, selection history, and diagnostics selection by stable source ID/line.
5. **Document organization.** Layers with current/visible/locked/export states; nested groups; linked/prefab groups with per-instance property overrides; status breadcrumbs.
6. **Schema-driven inspectors.** Entity/component/property metadata should generate fields, choices, flag editors, colors, asset pickers, help, defaults, multi-selection mixed values, and validation. This is the bridge to Cypher ECS rather than a separate hard-coded entity panel.
7. **Asset browsers.** Shared virtualized tile/list browser with usage/name sorting, grouping, used-only filtering, search, drag/drop, reveal, apply, and context selection.
8. **Issue service.** Validator registry, delayed incremental runs, filterable issues, source/object navigation, hide/show, batch-safe quick fixes, and severity.
9. **Build/run profiles.** Ordered typed tasks, variable expansion/completion, dry-run, captured output, cancellation, and engine/runtime launch profiles.
10. **Documentation export.** Generate the shortcut/action reference and menu paths from the command registry. CYKEYMAP should reference stable command IDs and store multiple bindings with platform/context overrides.

### TileEditor-specific adaptation

The TileEditor should reuse the kernel but expose tile-native tools:

- Pencil, eraser, line, rectangle, ellipse, flood fill, stamp/brush, random/weighted brush, terrain/autotile paint, collision paint, object/entity placement, selection/move/duplicate, transform, and layer tools become contextual command/tool states.
- Replace TrenchBroom's XYZ grid control with tile size, subgrid, snap, active tile layer, elevation/height slice, and tileset palette controls.
- Keep TrenchBroom's direct manipulation rules: Shift constraint, Alt alternate plane/behavior, Ctrl duplicate/toggle, Escape unwind, Return commit. Consistency across editors matters more than duplicating exact keys.
- Reuse layer visibility/lock/export controls for tile, collision, navigation, trigger, entity, lighting, audio, and decoration layers.
- Reuse linked groups as prefab/stamp instances. Per-instance protected properties map naturally to ECS component overrides.
- Reuse the issue browser for orphaned tile IDs, missing assets, invalid autotile transitions, overlapping unique spawns, broken entity references, off-grid collision, inaccessible navigation regions, and export bounds.

### Mason-specific adaptation

Mason needs the full 3D path: multi-view layouts; camera navigation; hierarchy-aware selection; transform gizmos; snapping; primitive generation; mesh/brush/patch or future modeling tools; material/UV inspection; light/audio/trigger/particle/entity component inspectors; visibility cells/portals; collision/nav overlays; world layers; prefabs; and build/run profiles. TrenchBroom's brush algorithms can inspire graybox tooling, while Cypher's long-term scene representation should keep authored geometry, compiled collision, renderer data, and ECS runtime data distinct.

### Improvements beyond TrenchBroom

The audit also exposes opportunities to improve the model:

- Add a searchable command palette showing current binding, context, disabled reason, and check state.
- Give every popup/context action a stable ID too. TrenchBroom's dynamic layer/material/issue menu actions are local QActions and cannot all be rebound or documented.
- Store semantic input chords (primary modifier, alternate modifier, pointer gesture) in CYKEYMAP, then resolve them per platform. Qt standard sequences hide some of this conversion inside Qt.
- Separate command availability from visibility so unavailable game/format tools can be discoverable with a reason.
- Add explicit input-priority diagnostics to show which tool consumed an event.
- Make issue severity and suppression persistent by validator ID plus stable object/property identity.
- Generate reference documentation, shortcut cheat sheets, and automation schemas from the same registry in CI and test that every documented ID resolves.
- Preserve the current action ID when menu labels or hierarchy change; add migrations only for IDs.

## Coverage inventory

### Counts

| Corpus | Files indexed | Lines indexed | Audit use |
|---|---:|---:|---|
| `lib/TbUiLib/src/*.cpp` | 179 | 41,870 | All UI classes and visible strings indexed; command and surface files below deep-read. |
| `lib/TbUiLib/include/ui/*.h` | 182 | 14,810 | Public UI inventory, contexts, state, and inheritance indexed. |
| `lib/TbAppLib` input/tool sources | 51 | 15,721 | Controller/gesture inventory indexed; active controllers deep-read or traced through manual and command dispatch. |
| `lib/TbAppLib` input/tool headers | 56 | 5,718 | Tool composition and event-consumer inventory. |
| `lib/TbMdlLib` validator sources/headers | 42 | 2,416 | All registered validators and quick fixes audited. |
| `app/DumpShortcuts` | 4 | 633 | Full generator path audited. |
| Manual + shortcut helper/README | 3 | 3,200 | Full manual structure and all interaction sections audited. |
| **Total** | **517** | **84,368** | Repository-wide relevant surface. |

### Deep-read UI and command files

Command/menu/keybinding core:

- `lib/TbUiLib/include/ui/Action.h`
- `lib/TbUiLib/src/Action.cpp`
- `lib/TbUiLib/include/ui/ActionContext.h`
- `lib/TbUiLib/src/ActionContext.cpp`
- `lib/TbUiLib/src/ActionBuilder.cpp`
- `lib/TbUiLib/src/ActionManager.cpp`
- `lib/TbUiLib/src/ActionMenu.cpp`
- `lib/TbUiLib/src/ActionExecutionContext.cpp`
- `lib/TbUiLib/src/ActionInfo.cpp`
- `lib/TbUiLib/src/KeyboardShortcutModel.cpp`
- `lib/TbUiLib/src/KeyboardPreferencePane.cpp`
- `lib/TbUiLib/src/KeyboardShortcutItemDelegate.cpp`
- `lib/TbUiLib/src/KeySequenceEdit.cpp`
- `lib/TbUiLib/src/LimitedKeySequenceEdit.cpp`
- `lib/TbUiLib/src/StandardShortcut.cpp`
- `lib/TbUiLib/src/MapDocumentActionCache.cpp`
- `lib/TbUiLib/src/EnableDisableTagCallback.cpp`
- `lib/TbPreferencesLib/include/prefs/Preferences.h`
- `lib/TbPreferencesLib/src/Preferences.cpp`
- `app/DumpShortcuts/src/Main.cpp`
- `app/DumpShortcuts/src/KeyStrings.cpp`
- `app/TrenchBroom/cmake/GenerateManual.cmake`
- `app/TrenchBroom/resources/documentation/manual/{README.md,index.md,shortcuts_helper.js}`

Main shell, views, popups, status, and context menus:

- `MapWindow.cpp`, `MapView.cpp`, `MapViewBase.cpp`, `MapView2D.cpp`, `MapView3D.cpp`
- `MapViewContainer.cpp`, `SwitchableMapViewContainer.cpp`, `CyclingMapView.cpp`
- `OnePaneMapView.cpp`, `TwoPaneMapView.cpp`, `ThreePaneMapView.cpp`, `FourPaneMapView.cpp`
- `MapViewActivationTracker.cpp`, `MapViewBar.cpp`, `MapViewToolBox.cpp`
- `ViewEditor.cpp`, `WidgetState.cpp`, `Splitter.cpp`, `TabBook.cpp`, `TabBar.cpp`
- `InfoPanel.cpp`, `Inspector.cpp`, `Console.cpp`, `IssueBrowser.cpp`, `IssueBrowserView.cpp`
- `lib/TbMdlLib/src/Map.cpp`, every `*Validator.cpp`, and `IssueQuickFix.cpp`

Inspector/browser/tool-page files:

- `MapInspector.cpp`, `LayerEditor.cpp`, `LayerListBox.cpp`, `ModEditor.cpp`
- `EntityInspector.cpp`, `EntityBrowser.cpp`, `EntityBrowserView.cpp`, `EntityDefinitionFileChooser.cpp`
- `EntityPropertyEditor.cpp`, `EntityPropertyGrid.cpp`, `EntityPropertyModel.cpp`, `EntityPropertyTable.cpp`, `EntityPropertyItemDelegate.cpp`
- `SmartPropertyEditorManager.cpp`, `SmartChoiceEditor.cpp`, `SmartFlagsEditor.cpp`, `SmartColorEditor.cpp`, `SmartWadEditor.cpp`, `SmartDefaultPropertyEditor.cpp`, `FlagsEditor.cpp`, `FlagsPopupEditor.cpp`
- `FaceInspector.cpp`, `FaceAttribsEditor.cpp`, `UvEditor.cpp`, `UvView.cpp`, `MaterialBrowser.cpp`, `MaterialBrowserView.cpp`, `MaterialCollectionEditor.cpp`
- `DrawShapeToolPage.cpp`, `DrawShapeToolExtensionPages.cpp`, `RotateToolPage.cpp`, `SweepToolPage.cpp`, `ScaleToolPage.cpp`, `ControlPointToolPage.cpp`

Dialogs/preferences/workflows:

- `WelcomeWindow.cpp`, `AppInfoPanel.cpp`, `GameDialog.cpp`, `AboutDialog.cpp`, `CrashDialog.cpp`
- `PreferenceDialog.cpp`, `GamesPreferencePane.cpp`, `ViewPreferencePane.cpp`, `ColorsPreferencePane.cpp`, `ColorModel.cpp`, `MousePreferencePane.cpp`, `UpdatePreferencePane.cpp`
- `GameEngineDialog.cpp`, `GameEngineProfileManager.cpp`, `GameEngineProfileEditor.cpp`, `GameEngineProfileListBox.cpp`, `CurrentGameIndicator.cpp`
- `LaunchGameEngineDialog.cpp`, `LaunchGameEngine.cpp`
- `CompilationDialog.cpp`, `CompilationProfileManager.cpp`, `CompilationProfileEditor.cpp`, `CompilationTaskListBox.cpp`, `CompilationRun.cpp`, `CompilationRunner.cpp`, `CompilationVariables.cpp`, `VariableStoreModel.cpp`
- `ObjExportDialog.cpp`, `ReplaceMaterialDialog.cpp`, `ChoosePathTypeDialog.cpp`
- `lib/UpdateLib/src/UpdateDialog.cpp`, `UpdateIndicator.cpp`

Input/tool controllers traced for gestures and mode ownership:

- `CameraTool2D.*`, `CameraTool3D.*`, `SelectionTool.*`, `InputState.*`, `ToolController.*`, `ToolChain.*`
- `DrawShapeToolController2D.*`, `DrawShapeToolController3D.*`, `AssembleBrushToolController3D.*`
- `MoveObjectsToolController.*`, `ExtrudeToolController.*`, `ClipToolController.*`
- `RotateToolController.*`, `RotateHandleController.*`, `SweepToolController.*`, `ScaleToolController.*`, `ShearToolController.*`
- `NodeHandleToolControllerBase.*`, `NodeHandleToolControllerParts.*`, `VertexToolController.*`, `EdgeToolController.*`, `FaceToolController.*`, `ControlPointToolController.*`
- `CreateEntityToolController.*`, `SetBrushFaceAttributesTool.*`
- `UvCameraTool.*`, `UvOffsetTool.*`, `UvOriginTool.*`, `UvRotateTool.*`, `UvScaleTool.*`, `UvShearTool.*`

The appendices below contain the exhaustive static command inventory and input bindings. Dynamic actions are described by generation rule because their concrete labels/count depend on the loaded game configuration and entity-definition file.
## Mouse and pointer gesture matrix

“Ctrl/Cmd” below means Command on macOS and Control elsewhere. Unless stated otherwise, button tests are exact, so extra buttons or modifiers can prevent a gesture. Modifiers can be changed during HandleDragTracker drags where the tracker explicitly updates its configuration.

| Mode / scope | Input | Behavior | Source |
|---|---|---|---|
| Any map view | Unhandled right click | Opens the context menu. | `ToolBoxConnector.cpp:333-343` |
| Any active drag | Escape / cancel event | Cancels the transaction/drag before tool deactivation. | `ToolBoxConnector.cpp:455-462`; `ToolBox.cpp:266-280` |
| 3D camera | Wheel, no Ctrl/Cmd or Alt | Moves forward/back; preference can use ray toward cursor. | `CameraTool3D.cpp:42-48,277-305` |
| 3D camera | Shift+wheel | Changes camera FOV zoom; macOS reads the swapped X axis. | `CameraTool3D.cpp:277-295` |
| 3D camera | Right drag, no modifiers | Free look. | `CameraTool3D.cpp:50-55,307-324` |
| 3D camera | Alt+right drag | Orbits around the picked point (or a default point). Wheel during the drag changes orbit radius. | `CameraTool3D.cpp:64-69,134-173,307-319` |
| 3D camera | Middle drag, none or Alt | Pans; with Alt Move preference and Alt held, vertical mouse motion moves along view direction. | `CameraTool3D.cpp:57-62,217-256,326-329` |
| 3D camera | Wheel while right-look drag is active | Adjusts fly speed in 5% steps, clamped to 0.1–10 times base speed. | `CameraTool3D.cpp:175-203` |
| 2D camera | Wheel, no buttons/modifiers | Zooms around cursor. | `CameraTool2D.cpp:33-53,140-156` |
| 2D camera | Right drag | Pans. | `CameraTool2D.cpp:106-111,158-174` |
| 2D camera | Middle drag | Pans when Alt Move is disabled. | `CameraTool2D.cpp:106-111,158-174` |
| 2D camera | Alt+middle vertical drag | Zooms when Alt Move is enabled. | `CameraTool2D.cpp:113-119,158-174` |
| Object selection | Left click | Replaces selection with frontmost/2D-priority selectable object; void clears. | `SelectionTool.cpp:280-386` |
| Object selection | Ctrl/Cmd+left click | Adds/removes the object; converts away from face selection when needed. | `SelectionTool.cpp:280-386` |
| Face selection | Shift+left click | Selects clicked brush face; Ctrl/Cmd adds/toggles; void clears. | `SelectionTool.cpp:280-344` |
| Object/group selection | Left double click | Opens closed group, selects siblings, or closes current group when double-clicking outside/void. Ctrl/Cmd extends sibling selection. | `SelectionTool.cpp:389-495` |
| Face selection | Shift+left double click | Selects every face of the brush; Ctrl/Cmd extends. | `SelectionTool.cpp:424-437` |
| Face selection | Shift+Alt+left double click | Flood-selects connected coplanar faces across touching brushes; Ctrl/Cmd extends. | `SelectionTool.cpp:396-417` |
| Selection | Ctrl/Cmd+wheel | Drills the selection through stacked hits toward/away from camera. | `SelectionTool.cpp:175-205,498-511` |
| Grid | Ctrl/Cmd+Alt+wheel | Increases/decreases grid size. | `SelectionTool.cpp:138-149,498-506` |
| Paint selection | Ctrl/Cmd+left drag over an unselected object | Paint-selects objects crossed by the drag. | `SelectionTool.cpp:514-578` |
| Paint face selection | Ctrl/Cmd+Shift+left drag | Paint-selects crossed faces. | `SelectionTool.cpp:514-578` |
| Move selected objects | Left drag a transitively selected node, no modifiers or Alt | Moves selection; default is horizontal/view plane, Alt gives vertical Z in perspective. | `MoveObjectsToolController.cpp:105-134`; `MoveHandleDragTracker.h:146-185,397-484` |
| Duplicate selected objects | Ctrl/Cmd+left drag selected node (Alt may also be held) | Starts a duplicate transaction; clones once on first nonzero movement, then translates. | `MoveObjectsToolController.cpp:105-134`; `MoveObjectsTool.cpp:48-103` |
| Common move tracker | Shift during horizontal drag | Constrains to the dominant movement axis. | `MoveHandleDragTracker.h:153-180,421-430` |
| Common move tracker | Ctrl/Cmd during drag | Switches relative ↔ absolute grid snapping when delegate supports it. | `MoveHandleDragTracker.h:153-180,433-438` |
| Draw shape, 2D | Left drag with no Ctrl/Cmd and no selection | Draws on reference plane. Shift equalizes view-plane axes; Shift+Alt equalizes all XYZ axes. | `DrawShapeToolController2D.cpp:142-199,221-260` |
| Draw shape, 3D | Left drag with no Ctrl/Cmd/Alt at start and no selection | Starts on hit brush face or default point, draws on XY plane. Alt during drag changes height; Shift equalizes XY; Shift+Alt equalizes XYZ. | `DrawShapeToolController3D.cpp:71-103,159-226,249-280` |
| Brush/assemble tool, 3D | No-modifier left click on brush face | Adds one snapped point. | `AssembleBrushToolController3D.cpp:306-332` |
| Brush/assemble tool, 3D | No-modifier left double click on face | Adds all face vertices. | `AssembleBrushToolController3D.cpp:334-358` |
| Brush/assemble tool, 3D | No-modifier left drag on brush face | Adds a rectangular face/polygon. | `AssembleBrushToolController3D.cpp:145-184,360-366` |
| Brush/assemble tool, 3D | Shift+left drag existing assembled polygon | Duplicates/extrudes the polygon along its normal. | `AssembleBrushToolController3D.cpp:240-285,360-366` |
| Clip tool | No-modifier left click | Adds a clip point; 2D snaps on the view plane, 3D to brush-face geometry. | `ClipToolController.cpp:393-403` and delegate implementations earlier in file |
| Clip tool | No-modifier left double click brush face | Sets clip points from the matching face. | `ClipToolController.cpp:405-414` |
| Clip tool | No-modifier left drag empty location | Adds a point and drags it; supports constructing first/second clip points. | `ClipToolController.cpp:416-434` |
| Clip tool | No-modifier left drag existing point | Moves clip point with view-specific snapping. | `ClipToolController.cpp:505-527` |
| Extrude permanent tool | Shift+left drag handle | Extrudes. | `ExtrudeToolController.cpp:630-670,709-738` |
| Extrude permanent tool | Shift+Ctrl/Cmd+left drag | Split-extrudes. | same |
| Extrude permanent tool | Shift+Alt+left drag | Slides. | same |
| Extrude permanent tool | Shift+Ctrl/Cmd+Alt+left drag | Stamps. | same |
| Rotate tool | No-modifier left drag rotation ring | Rotates around ring axis, snapped to handle/grid angle; click selects ring/axis. | `RotateHandleController.cpp:65-151,169-245` |
| Rotate tool | Left drag center with Ctrl/Cmd and Shift up (Alt either) | Repositions rotation center using the common move tracker; Alt gives vertical movement in perspective, and Shift/Ctrl/Cmd can change constraint/snap after the drag starts. | `RotateHandleController.cpp:317-430`; `MoveHandleDragTracker.h:397-438` |
| Sweep tool | Ring/center gestures | Reuses the rotation-handle interactions; center relocates sweep destination. | `SweepToolController.cpp:104-140`; `RotateHandleController.cpp:153-245,317-430` |
| Sweep tool | No-modifier left drag spherical scale handle | Adjusts sweep scale. | `SweepToolController.cpp:38-46,68-77,104-140` |
| Scale tool | Left drag bbox face/edge/corner handle | Scales selected objects. Alt changes anchor from opposite handle to center; Shift makes scaling proportional (in 2D excluding camera axis). Modifiers update live. | `ScaleToolController.cpp:73-141,128-204,224-250` |
| Shear tool | No-modifier left drag bbox side | Shears sideways. | `ShearToolController.cpp:46-86,215-249` |
| Shear tool, perspective | Alt+left drag non-horizontal bbox side | Constrains shear vertically; Alt can be changed live. | `ShearToolController.cpp:112-166,215-249` |
| Vertex/edge/face/control-point tools | Left click handle | Selects one; Ctrl/Cmd toggles/adds; empty click deselects. | `NodeHandleToolControllerParts.h:102-148` |
| Vertex/edge/face/control-point tools | Left drag empty space | Rectangular lasso; by default contained handles toggle, Ctrl/Cmd forces selection/add. | `NodeHandleToolControllerParts.h:48-100,150-184` |
| Edge/face handle tools | Left drag selected/draggable handle, no modifier or Alt | Moves selected handles; Alt is vertical in perspective. | `NodeHandleToolControllerParts.h:262-376` |
| Vertex tool | Left drag handle, any combination of Shift/Ctrl/Cmd/Alt | Moves vertices; Shift variants add a new vertex, Alt vertical, Ctrl/Cmd absolute snap. | `VertexToolController.cpp:83-103`; common movement in `MoveHandleDragTracker.h` |
| Vertex tool | Shift+Alt+left click with exactly one selected vertex | Moves selected vertex directly onto another vertex handle. | `VertexToolController.cpp:60-81` |
| Control-point tool | Left drag handle with none/Alt/Ctrl/Cmd/Ctrl/Cmd+Alt | Moves patch control points; supports vertical and absolute snapping but no Shift-add behavior. | `ControlPointToolController.cpp:58-68` |
| Face attribute transfer, 3D | Alt+left click | Copies material and all non-content face attributes with projection wrapping. | `SetBrushFaceAttributesTool.cpp:110-137,255-295,385-427` |
| Face attribute transfer, 3D | Alt+Shift+left click | Copies material/attributes with rotation wrapping (Valve projection-capable maps). | same |
| Face attribute transfer, 3D | Alt+Ctrl/Cmd+left click | Copies material only. | same |
| Face attribute transfer, 3D | Same modifier + left double click | Applies transfer to every face of target brush; rolls back preceding single-click step so undo is atomic. | `SetBrushFaceAttributesTool.cpp:69-104,385-407` |
| Face attribute paint, 3D | Same modifier + left drag, exactly one selected source face | Paints transfer from face to face in one long transaction and preserves original selected face. | `SetBrushFaceAttributesTool.cpp:297-378` |
| Entity browser → map | Drag payload `entity:<classname>` and drop | Previews entity in 2D/3D, commits on drop, removes on leave. | `CreateEntityToolController.cpp:38-70,91-138` |
| UV view camera | Wheel | Zooms 0.1–10 around pointer. | `UvCameraTool.cpp:80-108` |
| UV view camera | Right or middle drag | Pans. | `UvCameraTool.cpp:110-121` |
| UV origin | No-modifier left drag origin or axis handle | Moves both axes or one selected axis. Ctrl/Cmd during drag disables vertex/grid/center snapping. | `UvOriginTool.cpp:258-303,329-400` |
| UV rotation | No-modifier left drag yellow rotation circle | Rotates about UV origin, snapping to face edges. Ctrl/Cmd during drag disables snapping. | `UvRotateTool.cpp:203-253,324-379` |
| UV rotation | Ctrl/Cmd+left drag anywhere | Starts rotation without requiring circle hit and performs unsnapped rotation. | `UvRotateTool.cpp:264-290,357-379` |
| UV scale | No-modifier left drag UV grid line/intersection | Scales selected UV axis/axes about origin; Ctrl/Cmd during drag disables snapping. | `UvScaleTool.cpp:196-265,290-331` |
| UV shear | Alt+left drag UV grid line (Ctrl/Cmd optional) | Shears selected UV axis when projection supports it; Ctrl/Cmd disables edge-angle snapping. | `UvShearTool.cpp:131-213,238-293` |
| UV offset | No-modifier left drag remaining canvas | Offsets texture coordinates; Ctrl/Cmd pressed after starting disables grid snapping. | `UvOffsetTool.cpp:78-127,149-162` |

UV controller order is Rotate → Origin → Scale → Shear → Offset → Camera, so specialized handles claim left drags before general offsetting; camera claims right/middle drags (`UvView.cpp:206-211`).

## Keyboard action inventory

The appendix below catalogs all 179 static actions. “Registry” distinguishes view-only shortcuts from menu actions; debug menu rows are omitted from release builds. Defaults are Qt portable strings; `Qt standard` rows intentionally resolve per platform. The table keeps the complete user-visible command and shortcut surface with a pinned source location while the preceding sections describe contexts, enablement, checked state, and execution behavior in prose.

| # | Registry | Command | Default | Source |
|---:|---|---|---|---|
| 1 | view | Create Brush | `Return` | `ActionManager.cpp:154` |
| 2 | view | Toggle Clip Side | `Ctrl+Return` | `ActionManager.cpp:166` |
| 3 | view | Perform Clip | `Return` | `ActionManager.cpp:176` |
| 4 | view | Perform Sweep | `Return` | `ActionManager.cpp:186` |
| 5 | view | Decrease Sweep Scale | `[` | `ActionManager.cpp:196` |
| 6 | view | Increase Sweep Scale | `]` | `ActionManager.cpp:205` |
| 7 | view | Move Forward | `Up` | `ActionManager.cpp:218` |
| 8 | view | Move Backward | `Down` | `ActionManager.cpp:228` |
| 9 | view | Move Left | `Left` | `ActionManager.cpp:238` |
| 10 | view | Move Right | `Right` | `ActionManager.cpp:248` |
| 11 | view | Move Up | `PgUp` | `ActionManager.cpp:258` |
| 12 | view | Move Down | `PgDown` | `ActionManager.cpp:268` |
| 13 | view | Duplicate and Move Forward | `Ctrl+Up` | `ActionManager.cpp:281` |
| 14 | view | Duplicate and Move Backward | `Ctrl+Down` | `ActionManager.cpp:293` |
| 15 | view | Duplicate and Move Left | `Ctrl+Left` | `ActionManager.cpp:305` |
| 16 | view | Duplicate and Move Right | `Ctrl+Right` | `ActionManager.cpp:313` |
| 17 | view | Duplicate and Move Up | `Ctrl+PgUp` | `ActionManager.cpp:323` |
| 18 | view | Duplicate and Move Down | `Ctrl+PgDown` | `ActionManager.cpp:333` |
| 19 | view | Roll Clockwise | `Alt+Up` | `ActionManager.cpp:346` |
| 20 | view | Roll Counter-clockwise | `Alt+Down` | `ActionManager.cpp:356` |
| 21 | view | Yaw Clockwise | `Alt+Left` | `ActionManager.cpp:366` |
| 22 | view | Yaw Counter-clockwise | `Alt+Right` | `ActionManager.cpp:376` |
| 23 | view | Pitch Clockwise | `Alt+PgUp` | `ActionManager.cpp:386` |
| 24 | view | Pitch Counter-clockwise | `Alt+PgDown` | `ActionManager.cpp:396` |
| 25 | view | Move Textures Up | `Up` | `ActionManager.cpp:408` |
| 26 | view | Move Textures Up (Coarse) | `Shift+Up` | `ActionManager.cpp:418` |
| 27 | view | Move Textures Up (Fine) | `Ctrl+Up` | `ActionManager.cpp:428` |
| 28 | view | Move Textures Down | `Down` | `ActionManager.cpp:438` |
| 29 | view | Move Textures Down (Coarse) | `Shift+Down` | `ActionManager.cpp:448` |
| 30 | view | Move Textures Down (Fine) | `Ctrl+Down` | `ActionManager.cpp:458` |
| 31 | view | Move Textures Left | `Left` | `ActionManager.cpp:468` |
| 32 | view | Move Textures Left (Coarse) | `Shift+Left` | `ActionManager.cpp:478` |
| 33 | view | Move Textures Left (Fine) | `Ctrl+Left` | `ActionManager.cpp:488` |
| 34 | view | Move Textures Right | `Right` | `ActionManager.cpp:498` |
| 35 | view | Move Textures Right (Coarse) | `Shift+Right` | `ActionManager.cpp:508` |
| 36 | view | Move Textures Right (Fine) | `Ctrl+Right` | `ActionManager.cpp:518` |
| 37 | view | Rotate Textures Clockwise | `PgUp` | `ActionManager.cpp:528` |
| 38 | view | Rotate Textures Clockwise (Coarse) | `Shift+PgUp` | `ActionManager.cpp:538` |
| 39 | view | Rotate Textures Clockwise (Fine) | `Ctrl+PgUp` | `ActionManager.cpp:548` |
| 40 | view | Rotate Textures Counter-clockwise | `PgDown` | `ActionManager.cpp:558` |
| 41 | view | Rotate Textures Counter-clockwise (Coarse) | `Shift+PgDown` | `ActionManager.cpp:568` |
| 42 | view | Rotate Textures Counter-clockwise (Fine) | `Ctrl+PgDown` | `ActionManager.cpp:578` |
| 43 | view | Reveal in texture browser | — | `ActionManager.cpp:588` |
| 44 | view | Flip textures horizontally | `Ctrl+F` | `ActionManager.cpp:596` |
| 45 | view | Flip textures vertically | `Ctrl+Alt+F` | `ActionManager.cpp:604` |
| 46 | view | Reset texture alignment | `Shift+R` | `ActionManager.cpp:612` |
| 47 | view | Reset texture alignment to world aligned | `Alt+Shift+R` | `ActionManager.cpp:620` |
| 48 | view | Make Structural | `Alt+S` | `ActionManager.cpp:630` |
| 49 | view | Toggle Show Entity Classnames | — | `ActionManager.cpp:640` |
| 50 | view | Toggle Show Group Bounds | — | `ActionManager.cpp:649` |
| 51 | view | Toggle Show Brush Entity Bounds | — | `ActionManager.cpp:657` |
| 52 | view | Toggle Show Point Entity Bounds | — | `ActionManager.cpp:666` |
| 53 | view | Toggle Show Point Entities | — | `ActionManager.cpp:675` |
| 54 | view | Toggle Show Point Entity Models | — | `ActionManager.cpp:683` |
| 55 | view | Toggle Show Brushes | — | `ActionManager.cpp:692` |
| 56 | view | Show Textures | — | `ActionManager.cpp:700` |
| 57 | view | Hide Textures | — | `ActionManager.cpp:708` |
| 58 | view | Hide Faces | — | `ActionManager.cpp:716` |
| 59 | view | Toggle Shade Faces | — | `ActionManager.cpp:724` |
| 60 | view | Toggle Show Fog | — | `ActionManager.cpp:732` |
| 61 | view | Toggle Show Edges | — | `ActionManager.cpp:740` |
| 62 | view | Show All Entity Links | — | `ActionManager.cpp:748` |
| 63 | view | Show Transitively Selected Entity Links | — | `ActionManager.cpp:756` |
| 64 | view | Show Directly Selected Entity Links | — | `ActionManager.cpp:765` |
| 65 | view | Hide All Entity Links | — | `ActionManager.cpp:774` |
| 66 | view | Cycle View | `Space` | `ActionManager.cpp:784` |
| 67 | view | Reset Camera Zoom | `Ctrl+Alt+Z` | `ActionManager.cpp:792` |
| 68 | view | Cancel | `Esc` | `ActionManager.cpp:800` |
| 69 | menu | New Document | Qt standard `New` | `ActionManager.cpp:826` |
| 70 | menu | Open Document... | Qt standard `Open` | `ActionManager.cpp:835` |
| 71 | menu | Save Document | Qt standard `Save` | `ActionManager.cpp:845` |
| 72 | menu | Save Document as... | Qt standard `SaveAs` | `ActionManager.cpp:853` |
| 73 | menu | Wavefront OBJ... | — | `ActionManager.cpp:863` |
| 74 | menu | Map... | — | `ActionManager.cpp:871` |
| 75 | menu | Load Point File... | — | `ActionManager.cpp:886` |
| 76 | menu | Reload Point File | — | `ActionManager.cpp:894` |
| 77 | menu | Unload Point File | — | `ActionManager.cpp:904` |
| 78 | menu | Load Portal File... | — | `ActionManager.cpp:915` |
| 79 | menu | Reload Portal File | — | `ActionManager.cpp:923` |
| 80 | menu | Unload Portal File | — | `ActionManager.cpp:933` |
| 81 | menu | Reload Material Collections | `F5` | `ActionManager.cpp:944` |
| 82 | menu | Reload Entity Definitions | `F6` | `ActionManager.cpp:954` |
| 83 | menu | Revert Document | — | `ActionManager.cpp:965` |
| 84 | menu | Close Document | Qt standard `Close` | `ActionManager.cpp:976` |
| 85 | menu | Undo | Qt standard `Undo` | `ActionManager.cpp:990` |
| 86 | menu | Redo | Qt standard `Redo` | `ActionManager.cpp:1002` |
| 87 | menu | Repeat Last Commands | `Ctrl+R` | `ActionManager.cpp:1014` |
| 88 | menu | Clear Repeatable Commands | `Ctrl+Shift+R` | `ActionManager.cpp:1022` |
| 89 | menu | Cut | Qt standard `Cut` | `ActionManager.cpp:1034` |
| 90 | menu | Copy | Qt standard `Copy` | `ActionManager.cpp:1046` |
| 91 | menu | Paste | Qt standard `Paste` | `ActionManager.cpp:1058` |
| 92 | menu | Paste at Original Position | `Ctrl+Alt+V` | `ActionManager.cpp:1070` |
| 93 | menu | Duplicate | `Ctrl+D` | `ActionManager.cpp:1081` |
| 94 | menu | Delete | Qt standard `Delete` | `ActionManager.cpp:1092` |
| 95 | menu | Flip Horizontally | `Ctrl+F` | `ActionManager.cpp:1105` |
| 96 | menu | Flip Vertically | `Ctrl+Alt+F` | `ActionManager.cpp:1116` |
| 97 | menu | Move... | `Ctrl+Alt+M` | `ActionManager.cpp:1127` |
| 98 | menu | Convex Merge | `Ctrl+J` | `ActionManager.cpp:1139` |
| 99 | menu | Subtract | `Ctrl+K` | `ActionManager.cpp:1149` |
| 100 | menu | Hollow | `Ctrl+Shift+K` | `ActionManager.cpp:1159` |
| 101 | menu | Intersect | `Ctrl+L` | `ActionManager.cpp:1169` |
| 102 | menu | Snap Vertices to Integer | `Ctrl+Shift+V` | `ActionManager.cpp:1181` |
| 103 | menu | Snap Vertices to Grid | `Ctrl+Alt+Shift+V` | `ActionManager.cpp:1191` |
| 104 | menu | Convert Selection to Patches | `Ctrl+P` | `ActionManager.cpp:1203` |
| 105 | menu | Texture Lock | — | `ActionManager.cpp:1215` |
| 106 | menu | UV Lock | `U` | `ActionManager.cpp:1225` |
| 107 | menu | Replace Material... | — | `ActionManager.cpp:1236` |
| 108 | menu | Select All | Qt standard `SelectAll` | `ActionManager.cpp:1249` |
| 109 | menu | Invert Selection | `Ctrl+Alt+A` | `ActionManager.cpp:1259` |
| 110 | menu | Deselect All | `Ctrl+Shift+A` | `ActionManager.cpp:1269` |
| 111 | menu | Select Siblings | `Ctrl+B` | `ActionManager.cpp:1280` |
| 112 | menu | Select Touching | `Ctrl+T` | `ActionManager.cpp:1290` |
| 113 | menu | Select Inside | `Ctrl+E` | `ActionManager.cpp:1300` |
| 114 | menu | Select Tall | `Ctrl+Shift+E` | `ActionManager.cpp:1310` |
| 115 | menu | Select by Line Number... | — | `ActionManager.cpp:1320` |
| 116 | menu | Group Selected Objects | `Ctrl+G` | `ActionManager.cpp:1335` |
| 117 | menu | Ungroup Selected Objects | `Ctrl+Shift+G` | `ActionManager.cpp:1345` |
| 118 | menu | Rename Selected Groups | `Ctrl+Alt+G` | `ActionManager.cpp:1355` |
| 119 | menu | Create Linked Duplicate | `Ctrl+Shift+D` | `ActionManager.cpp:1367` |
| 120 | menu | Select Linked Groups | — | `ActionManager.cpp:1377` |
| 121 | menu | Separate Selected Groups | — | `ActionManager.cpp:1387` |
| 122 | menu | Extract Selected Objects | — | `ActionManager.cpp:1397` |
| 123 | menu | Clear Protected Properties | — | `ActionManager.cpp:1407` |
| 124 | menu | Brush Tool | `B` | `ActionManager.cpp:1422` |
| 125 | menu | Clip Tool | `C` | `ActionManager.cpp:1438` |
| 126 | menu | Rotate Tool | `R` | `ActionManager.cpp:1452` |
| 127 | menu | Sweep Tool | `Y` | `ActionManager.cpp:1466` |
| 128 | menu | Scale Tool | `T` | `ActionManager.cpp:1480` |
| 129 | menu | Shear Tool | `G` | `ActionManager.cpp:1494` |
| 130 | menu | Vertex Tool | `V` | `ActionManager.cpp:1508` |
| 131 | menu | Edge Tool | `E` | `ActionManager.cpp:1523` |
| 132 | menu | Face Tool | `F` | `ActionManager.cpp:1538` |
| 133 | menu | Control Point Tool | `P` | `ActionManager.cpp:1553` |
| 134 | menu | Deactivate Current Tool | `Shift+Esc` | `ActionManager.cpp:1569` |
| 135 | menu | Show Grid | `0` | `ActionManager.cpp:1587` |
| 136 | menu | Snap to Grid | `Alt+0` | `ActionManager.cpp:1598` |
| 137 | menu | Increase Grid Size | `+` | `ActionManager.cpp:1609` |
| 138 | menu | Decrease Grid Size | `-` | `ActionManager.cpp:1619` |
| 139 | menu | Set Grid Size 0.125 | — | `ActionManager.cpp:1630` |
| 140 | menu | Set Grid Size 0.25 | — | `ActionManager.cpp:1641` |
| 141 | menu | Set Grid Size 0.5 | — | `ActionManager.cpp:1652` |
| 142 | menu | Set Grid Size 1 | `1` | `ActionManager.cpp:1663` |
| 143 | menu | Set Grid Size 2 | `2` | `ActionManager.cpp:1674` |
| 144 | menu | Set Grid Size 4 | `3` | `ActionManager.cpp:1685` |
| 145 | menu | Set Grid Size 8 | `4` | `ActionManager.cpp:1696` |
| 146 | menu | Set Grid Size 16 | `5` | `ActionManager.cpp:1707` |
| 147 | menu | Set Grid Size 32 | `6` | `ActionManager.cpp:1718` |
| 148 | menu | Set Grid Size 64 | `7` | `ActionManager.cpp:1729` |
| 149 | menu | Set Grid Size 128 | `8` | `ActionManager.cpp:1740` |
| 150 | menu | Set Grid Size 256 | `9` | `ActionManager.cpp:1751` |
| 151 | menu | Move Camera to Next Point | `.` | `ActionManager.cpp:1764` |
| 152 | menu | Move Camera to Previous Point | `,` | `ActionManager.cpp:1774` |
| 153 | menu | Reset 2D Cameras | `Ctrl+Shift+U` | `ActionManager.cpp:1784` |
| 154 | menu | Focus Camera on Selection | `Ctrl+U` | `ActionManager.cpp:1794` |
| 155 | menu | Move Camera to... | — | `ActionManager.cpp:1804` |
| 156 | menu | Isolate Selection | `Ctrl+I` | `ActionManager.cpp:1814` |
| 157 | menu | Hide Selection | `Ctrl+Alt+I` | `ActionManager.cpp:1824` |
| 158 | menu | Show All | `Ctrl+Shift+I` | `ActionManager.cpp:1834` |
| 159 | menu | Show Map Inspector | `Ctrl+1` | `ActionManager.cpp:1843` |
| 160 | menu | Show Entity Inspector | `Ctrl+2` | `ActionManager.cpp:1851` |
| 161 | menu | Show Face Inspector | `Ctrl+3` | `ActionManager.cpp:1861` |
| 162 | menu | Toggle Toolbar | `Ctrl+Alt+T` | `ActionManager.cpp:1870` |
| 163 | menu | Toggle Info Panel | `Ctrl+4` | `ActionManager.cpp:1881` |
| 164 | menu | Toggle Inspector | `Ctrl+5` | `ActionManager.cpp:1892` |
| 165 | menu | Maximize Current View | macOS physical `Ctrl+Space` (`Meta+Space` portable); other `Ctrl+Space` | `ActionManager.cpp:1903` |
| 166 | menu | Preferences... | Qt standard `Preferences` | `ActionManager.cpp:1920` |
| 167 | menu | Compile Map... | — | `ActionManager.cpp:1933` |
| 168 | menu | Launch Engine... | — | `ActionManager.cpp:1941` |
| 169 | menu | Re-run compilation... | — | `ActionManager.cpp:1951` |
| 170 | debug menu | Print Vertices to Console | — | `ActionManager.cpp:1968` |
| 171 | debug menu | Create Brush... | — | `ActionManager.cpp:1976` |
| 172 | debug menu | Create Cube... | — | `ActionManager.cpp:1984` |
| 173 | debug menu | Crash... | — | `ActionManager.cpp:1992` |
| 174 | debug menu | Throw Exception During Command | — | `ActionManager.cpp:2000` |
| 175 | debug menu | Show Crash Report Dialog... | — | `ActionManager.cpp:2008` |
| 176 | debug menu | Set Window Size... | — | `ActionManager.cpp:2016` |
| 177 | debug menu | Show Palette... | — | `ActionManager.cpp:2024` |
| 178 | menu | TrenchBroom Manual | Qt standard `HelpContents` | `ActionManager.cpp:2038` |
| 179 | menu | About TrenchBroom | — | `ActionManager.cpp:2046` |
