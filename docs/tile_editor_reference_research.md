# CypherTileEditor reference-editor research and product boundary

Research date: 2026-09-17.

This document turns primary-source research into a practical product boundary for
CypherTileEditor. The editor exists to create small `.cymap` test maps quickly,
exercise Cypher's renderer and runtime data path, and establish reusable editor
infrastructure. It is not Mason and should not acquire a general brush, mesh,
CSG, terrain, or entity-logic toolchain during this milestone.

The viewport input rules in
[`tile_editor_navigation_research.md`](tile_editor_navigation_research.md) are
the authoritative control contract. This document covers layout, authoring,
materials, build workflow, configuration, and scope.

## Research provenance

| Reference | Revision or official source | What was inspected |
| --- | --- | --- |
| TrenchBroom | Stable `v2026.2`, commit [`4c756be1`](https://github.com/TrenchBroom/TrenchBroom/tree/4c756be1a54a28d921d18c77716a5f170bdc31d0), and the official [2026.2 manual](https://trenchbroom.github.io/manual/latest/). | Map-view activation, 2D/3D cameras, linked views, four-pane splitters, maximize/restore, permanent and modal tools, shortcuts, selection, and the material browser. Relevant source includes [`FourPaneMapView.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/FourPaneMapView.cpp), [`KeyboardShortcutModel.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/KeyboardShortcutModel.cpp), and [`MaterialBrowser.cpp`](https://github.com/TrenchBroom/TrenchBroom/blob/4c756be1a54a28d921d18c77716a5f170bdc31d0/lib/TbUiLib/src/MaterialBrowser.cpp). |
| NetRadiant | Upstream commit [`b4b295d7`](https://gitlab.com/xonotic/netradiant/-/tree/b4b295d7a37797cc2752e48aa8ce42492e7016f0). | Regular, split, floating, and four-view layouts; orthographic activation; camera and grid preferences; command registration; build definitions; output console. Relevant source: [`mainframe.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/mainframe.cpp), [`grid.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/grid.cpp), [`build.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/build.cpp), and [`console.cpp`](https://gitlab.com/xonotic/netradiant/-/blob/b4b295d7a37797cc2752e48aa8ce42492e7016f0/radiant/console.cpp). |
| Q3Edit | Upstream commit [`02f87647`](https://github.com/drdator/q3edit/tree/02f87647162e5bf5e39fe61968f904efe8e19675) and official [release notes](https://q3edit.com/release-notes.html). | Quad, single, two-pane, and three-pane layouts; selection and transforms; command/shortcut registry; themes/preferences; structured build results. Relevant source: [`preferences.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/preferences.ts), [`editor-commands.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/editor-commands.ts), and [`build-panel.ts`](https://github.com/drdator/q3edit/blob/02f87647162e5bf5e39fe61968f904efe8e19675/src/build-panel.ts). |
| J.A.C.K. | Official [reference manual](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/496450/manuals/VDKManual.pdf?t=1736824236) and [feature list](https://jack.hlfx.ru/en/features.html). | Active 2D view behavior, creation in every orthographic plane, grids, selection/clone behavior, color schemes, cameras, autosave, backups, visibility groups, and material work. |
| Valve Hammer | Valve's archived [3D/2D view guide](https://valvearchive.com/Software/Hammer%20Editor/Documents/Half-Life%20Hammer%203.4%20Guide%20%28April%202002%29/html/the_3d_and_2d_views.htm), [View menu](https://valvearchive.com/hammer/guides/wc_3.4/The_View_Menu.htm), [texturing guide](https://valvearchive.com/Software/Hammer%20Editor/Documents/Half-Life%20Hammer%203.4%20Guide%20%28April%202002%29/html/texturing_solids.htm), and [map operations](https://valvearchive.com/Software/Hammer%20Editor/Documents/Half-Life%20Hammer%203.4%20Guide%20%28April%202002%29/html/map_operations.htm); public Source 2 [overview](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Hammer_Overview.htm). | Four-view editing, changeable view roles, active-pane cues, tool properties, selection modes, material operations, compile/run workflow, and editor panels. The Source 2 material is historical evidence, not a guarantee of current CS2 defaults. |

`Hammer 5 Tools` is a separate community toolkit; it is not Valve's version
name for the Hammer editor. Visual screenshots are useful references, but the
behavioral claims above come from the linked manuals and source.

## Findings that apply directly to a tile editor

The references converge on a small set of workflow principles:

- A four-pane workspace is a strong default, but users need single, two-pane,
  three-pane, maximize, hide, restore, and role-reassignment workflows.
- Navigation and selection are always available. Shape or drawing tools are
  modal layers on top of them.
- A creation or transform drag previews across every view and commits as one
  undo transaction. Escape cancels it completely.
- Selection, material assignment, exact properties, and build diagnostics are
  synchronized views of one document, not separate stores.
- Visible grid density, edit snapping, and geometry snapping are different
  features and must not silently change one another.
- Search, filters, recent items, favorites, and tags scale better than one huge
  unstructured palette of icons.
- Professional polish comes from stable interaction rules and recoverability,
  not from reproducing every tool in a mature general-purpose editor.

## Workspace and visual system

The default Cypher workspace should be compact and readable at ordinary desktop
sizes:

```text
menu + compact command toolbar
+-------------------------------------------------------+-------------------+
| icon-only tool rail | 3D Camera      | Top           | Objects           |
|                     +----------------+---------------+ Properties         |
|                     | Front          | Side          | Materials / Pieces |
+-------------------------------------------------------+-------------------+
| command/build/diagnostic output (bottom dock, optional)                   |
+---------------------------------------------------------------------------+
| coordinates | selection | edit grid | snap | build state | renderer state  |
```

The exact arrangement remains configurable. The default contract is:

- top-left starts as the renderer-backed 3D map view;
- the other panes start as Top, Front, and Side;
- every pane has a compact role selector and context menu;
- any pane may become any supported role, including a duplicate Top view;
- hiding a pane preserves its role and camera so it can be restored;
- maximize/restore affects the hovered pane;
- splitter ratios and pane roles persist per workspace;
- splitters use a restrained editor gutter, approximately the six-pixel class
  used by TrenchBroom, rather than wide decorative bars;
- one active/input pane receives a thin configurable accent; hover never washes
  the entire viewport with a highlight;
- opening or closing Materials, Objects, Properties, Pieces, or Validation only
  changes that named dock. It must not raise or substitute another dock;
- the console/build output stays in the bottom dock by default; authoring docks
  occupy the right side; the left rail remains icon-only.

The look may use the restrained dark graphite, blue drafting view, and orange
selection language seen in professional editors, implemented through Cypher's
own QSS tokens and icons. Axis colors remain conventional: X red, Y green, Z
blue. Validate, Build, Run, Stop, warning, and error controls may use semantic
color; ordinary tool icons should remain legible monochrome glyphs with an
orange selected state. Cypher must create or license its own icon set.

On startup, a new or loaded map fits all relevant views once. Restoring a saved
workspace pose takes priority over automatic fit. A document resize updates
bounds and validation immediately without destroying deliberate camera poses.

## Selection and direct manipulation

The tile editor needs a complete tile-scale selection loop:

1. LMB selects one compatible element.
2. Ctrl/Cmd+LMB toggles membership; Shift+LMB adds.
3. Dragging empty space creates a marquee. A preference chooses `touching` or
   `fully contained` behavior.
4. Selection is shared across Top, Front, Side, 3D, Objects, and Properties.
5. `Ctrl/Cmd+A` selects compatible visible elements; Escape follows the cancel
   priority in the navigation contract.
6. Dragging a selection moves it in the visible plane; arrow keys nudge by the
   edit increment; Page Up/Down changes the hidden axis or floor/elevation.
7. `Ctrl/Cmd+D` duplicates with a predictable grid offset; Shift-drag may
   duplicate interactively.
8. The property panel accepts exact values and reports multi-selection values as
   common, mixed, or unavailable.
9. Every gesture previews in all panes, commits one command, and assigns fresh
   stable IDs to duplicated authored objects.

Top, Front, and Side must all support creation. The plane supplies two
dimensions and the current preset or last-used value supplies the hidden one:

| View | Drag controls | Preset supplies |
| --- | --- | --- |
| Top | width and depth | height/elevation |
| Front | width and height | depth |
| Side | depth and height | width |

The current cell map can support floors/rooms, wall boundaries, doors/openings,
stairs or ramps with constrained parameters, spawn markers, trigger volumes,
lights/markers, erase, and tile-appropriate rotate/mirror. Preview geometry must
appear in every pane before commit. Enter may commit a keyboard-driven preview;
Escape cancels.

Generated walls are currently consequences of occupied cells. They cannot be
moved as durable independent objects until `.cymap` has authored wall records
with IDs, edge ownership, suppression of the generated boundary, dimensions,
orientation, materials, persistence, validation, and renderer output. A toolbar
icon cannot solve that data-model requirement.

## Grid, axes, depth, and overlays

Cypher should expose three separate systems:

- **edit grid:** the increment used to create, resize, and nudge;
- **display grid:** minor and major lines chosen for the current zoom;
- **geometry snap:** optional snapping to existing corners, edges, centers, or
  authored attachment points.

Adaptive zoom changes only display density. The edit grid remains explicit in
the toolbar and status bar. Each orthographic pane can independently show:

- the conventional X/Y/Z orientation indicator;
- world-origin axes located at the actual world origin;
- major and minor grid lines;
- numbered rulers;
- cursor coordinates;
- view name, zoom, edit grid, snap state, and represented slice/floor;
- selection dimensions and transform preview;
- optional labels, material colors, texture preview, and depth fading.

Every overlay, units-per-pixel display, label, ruler, axis emphasis, line width,
depth tint, selection tint, active border, and hover cue belongs in Appearance
or View settings. Disabling one must not alter the map, snap increment, or
active-pane routing.

Front and Side must use elevation and material information from the same
generated geometry as the 3D preview. A flat single-color rectangle is
insufficient when wall level, floor level, stairs, or material slot differs.

## Materials and pieces

The material browser is an independent right-side dock with:

- text search, categories/tags, favorites, and recent materials;
- `used in this map` and compatible-selection filters;
- configurable thumbnail size;
- current material preview and semantic slot;
- apply to selection, select all users, and replace-in-selection/map commands;
- material-colored or textured orthographic preview;
- clear missing-material and incompatible-slot diagnostics.

The tile editor should assign semantic slots such as floor, ceiling, wall
interior, wall exterior, trim, stair tread, and stair riser. `.cymat` and
`.cytex` can later provide renderer-backed assets behind this interface. The
document stores stable asset references and semantic assignments rather than UI
thumbnail state.

Pieces/Shapes is another searchable library. It should contain parameterized
tile assemblies and presets: rooms, corridor sections, wall corners, doors,
stairs, ramps, spawns, triggers, and markers. Store dimensions, compatible
planes, orientation, tags, and a thumbnail recipe. Load visible thumbnails on
demand and promote favorites/recent pieces; do not attempt to display millions
of entries at once.

Per-face UV projection, arbitrary texture seams, patch texturing, and complete
face alignment belong in Mason because the present tile representation cannot
preserve those edits.

## Commands, console, validation, build, and play

A professional editor console should be backed by a registered command system,
not by parsing ad hoc strings in each widget. It should provide:

- command and argument completion;
- history with prefix search;
- searchable help and aliases;
- quoted paths and deterministic parsing;
- structured success, warning, and error entries;
- commands such as `view.layout quad`, `view.fit selection`, `grid.set 16`,
  `select.material stone`, `validate`, `build`, and `play`;
- click-through diagnostics that select and frame the relevant map element.

The build/output area is related but distinct. Its stages are explicit:

```text
validate document -> generate geometry -> write/export -> launch playtest
```

It needs stage progress, cancel, stale-result indication after edits, clear,
copy/save log, structured diagnostics, build history, Quick Play, and Repeat
Last Build. Building the selection or an authored region can be added when the
geometry pipeline has a stable incremental boundary.

NetRadiant's console is intentionally read-only output, while its build menu is
data-driven. Q3Edit adds live stages, cancellation, history, diagnostics, and
play output. These are stronger references for normal map work than embedding
an unrestricted zsh/bash process as the primary console. A developer terminal
may become a separate optional dock later, after process lifetime, working
directory, environment, quoting, encoding, cancellation, and security are
designed. It is not required to finish CypherTileEditor.

## Configuration ownership

The editor needs configuration, but not one untyped file that owns everything:

| State | Owner | Required behavior |
| --- | --- | --- |
| Theme, QSS profile, UI scale, font/icon size, palette, overlay visibility, input sensitivity, camera defaults, shortcuts | User preferences / exported editor profile | Apply without dirtying the map. Parse and validate a complete candidate before activation. Detect shortcut conflicts. |
| Dock geometry, splitter ratios, pane roles, hidden/maximized panes, camera poses, local isolation | Workspace/session state | Restore locally. A color theme change does not rearrange panes. A layout reset does not rewrite the map. |
| Dimensions, units, cells, markers, doors, materials, future authored walls and pieces | Typed `.cymap` document state serialized through CYKV | Change through validated document commands, mark dirty, notify every view, and support undo/redo. Runtime dimension changes do not require reload. |
| Asset roots, build/export presets, renderer executable, gameplay definitions | Project configuration | Keep portable project data separate from personal machine paths. Introduce only when the project workflow needs it. |

CYKV is serialization, not the live document API. UI code modifies typed editor
state through validated commands; save/load translates that state to and from
CYKV. Direct mutation of arbitrary serialized keys would bypass invariants,
stable IDs, history, validation, and renderer rebuild notifications.

### Color-theme contract

TrenchBroom keeps appearance preferences separate from document state, while
Q3Edit exposes themes through global Preferences rather than through a map or
project. Cypher follows that boundary. A color theme is a portable palette; an
editor configuration profile remains the broader mechanism for camera,
viewport, shortcut, grid, and workflow defaults.

CypherTileEditor uses these rules:

- built-in and user themes share the same semantic color roles;
- choosing a theme previews it immediately across Qt chrome and every viewport;
- Apply or OK atomically commits the active preferences, while Cancel restores
  the last successfully applied appearance;
- a `.cytheme` contains only a display name and the complete color palette;
  it cannot modify shortcuts, camera behavior, pane layout, map dimensions, or
  project paths;
- theme colors use strict opaque `#RRGGBB` values; transient selection and
  overlay opacity remains editor-controlled;
- users can save, import, export, and delete themes without making the current
  editor appearance depend on that theme file continuing to exist;
- unmatched manual edits appear as `Custom (modified)` rather than being
  mislabeled as a built-in theme;
- the bundled QSS owns selectors, spacing, and widget structure. Users edit
  semantic color inputs instead of injecting arbitrary QSS;
- error, warning, success, information, muted text, shadows, and axis underlays
  are derived from the palette and contrast-corrected for the active background;
- authored material colors, texture previews, and missing-material artwork stay
  independent of the UI theme because they describe map content.

The default user-theme directory is the platform application-configuration
directory under `themes/`. Theme discovery is deterministic, skips symbolic
links, rejects malformed or oversized files without partially applying them,
and writes replacements atomically. The active color values are also copied
into `editor.ini`; deleting a theme therefore never breaks startup or changes a
map.

The preference surface should include:

- theme, colors, QSS profile, UI scale, font, icon size;
- viewport background, grid, major grid, axes, selection, wireframe, and depth
  colors and line widths;
- active-pane cue, splitter width, rulers, metrics, labels, and material preview;
- default layout, pane roles, linked 2D cameras, and startup fit behavior;
- look/pan/zoom sensitivity, speed, fast/slow multipliers, FOV, inversion,
  clipping, and camera mode;
- edit grid, snapping, marquee policy, and transform preview;
- every command shortcut, with search, conflicts, restore default, import, and
  export;
- autosave, recovery, backup count, recent files, materials, pieces, and
  commands.

## Delivery boundary

### Finish in CypherTileEditor

- reliable save/load, live dimension/property changes, dirty state, recovery,
  and undo/redo transactions;
- default quad layout plus single, two-pane, three-pane, maximize, hide/restore,
  equalize, reassignable roles, duplicate roles, and persisted splitters;
- the complete viewport input contract, selection-aware framing, explicit orbit
  pivot, camera capture cleanup, and optional linked orthographic cameras;
- shared click/marquee/multi-selection across 2D, 3D, Objects, and Properties;
- tile-scale move, nudge, duplicate, copy/paste, delete, rotate/mirror, exact
  properties, and cross-view preview;
- creation from Top, Front, and Side for floor/room, boundary wall, door,
  constrained stair/ramp, spawn, trigger, light/marker, and erase;
- levels/heights, map dimensions, semantic material slots, material preview,
  searchable Pieces and Materials docks;
- strict independent dock toggles, own icon set, configurable QSS themes,
  overlays, shortcuts, camera settings, and workspace persistence;
- editor command line, validation, structured build output, geometry export, and
  renderer/playtest launch;
- renderer-backed WYSIWYG 3D map preview through a narrow runtime/editor bridge.

### Prepare for reuse, without building Mason here

- stable authored IDs and explicit selection references;
- typed commands with one transaction per gesture;
- document notifications and derived render geometry;
- serializable pane roles and view settings;
- asset references and semantic material slots;
- validation diagnostics that point back to authored elements.

### Defer to Mason

- arbitrary convex brushes and independent vertex/edge/face component modes;
- clipping planes; general CSG carve, hollow, merge, subtract, and intersect;
- arbitrary mesh creation, topology editing, deformation, curves, patches, and
  terrain sculpting;
- full face transform gizmos, pivots, UV projection, seam, and alignment tools;
- a broad entity-class/property system, visual logic graphs, scripting, and
  complex prefab inheritance;
- portal, leak, visibility, lighting, navigation, physics, and large-world debug
  suites;
- project-wide asset authoring, multi-user editing, and unrestricted integrated
  system terminals.

This boundary still allows authored walls later in the tile editor if they are
strictly rectangular, grid-aware tile objects. It excludes general brush and
face editing, which would duplicate Mason prematurely.

## Licensing and asset provenance

TrenchBroom's code is licensed under
[`GNU GPL version 3`](https://github.com/TrenchBroom/TrenchBroom/blob/v2026.2/LICENSE.txt),
and its repository credits third-party icons and assets. The source may be read
to understand behavior, state transitions, and edge cases. Cypher's
implementation must be written independently. Do not copy or adapt TrenchBroom
code, icons, shaders, artwork, or resources into this proprietary repository
without an explicit licensing decision and an asset-by-asset provenance audit.

Screenshots from Hammer, Q3Edit, J.A.C.K., NetRadiant, and TrenchBroom guide
layout and interaction study; they do not grant rights to their iconography,
textures, branding, or source. Cypher needs original or clearly licensed visual
assets, with license and attribution recorded beside each imported asset.

## Product acceptance criteria

- A new map opens fitted and usable at common window sizes; a restored workspace
  retains deliberate pane roles, splits, and camera poses.
- Any pane can change role, duplicate another role, maximize, hide, restore, or
  close without destroying document or camera state.
- Each toolbar dock action affects exactly one named dock.
- The pointer determines viewport-command context without a preparatory click;
  text entry and active mouse gestures remain protected.
- Top, Front, and Side can create and manipulate supported tile geometry with a
  live preview in all other views.
- A transform commits one undo entry; Escape and focus loss leave no partial
  document mutation.
- Grid adaptation never changes edit snap. Axes meet at world origin and all
  metrics/overlays can be configured or disabled.
- Material identity is visible in orthographic and 3D views; missing references
  produce actionable diagnostics.
- Runtime edits to dimensions and typed `.cymap` properties update views,
  validation, generated geometry, and dirty state without reload.
- Validate, Build, and Play show stage/state, can be canceled where possible,
  and report diagnostics that select the offending authored element.
- A malformed preference profile or document edit is rejected atomically and
  preserves the previous working state.
- No referenced editor code or visual asset enters Cypher without a recorded,
  compatible license and provenance decision.
